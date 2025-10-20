#!/usr/bin/env python3
"""
auxv.print.py - Pretty-print Linux auxv (AT_* entries) and optionally decode HWCAP bits.

Two modes of operation:
  1. File mode: Read auxv from a binary file
     ./auxv.print.py --decode-auxv <file> [--decode-arch <arch>]

  2. Self mode: Read from /proc/self/auxv (default)
     ./auxv.print.py [--try-strings] [--decode-hwcap]

Examples:
  ./auxv.print.py --decode-auxv auxv.bin --decode-arch aarch64
  ./auxv.print.py --decode-hwcap --try-strings
"""

import argparse
import ctypes
import os
import platform
import struct
import sys


SUPPORTED_ARCH_LIST = ("aarch64", "x86_64", "i386", "i686")

# ---- AT_* tag names (from linux/auxvec.h) ----
AT_NAMES = {
    0: "AT_NULL",
    1: "AT_IGNORE",
    2: "AT_EXECFD",
    3: "AT_PHDR",
    4: "AT_PHENT",
    5: "AT_PHNUM",
    6: "AT_PAGESZ",
    7: "AT_BASE",
    8: "AT_FLAGS",
    9: "AT_ENTRY",
    10: "AT_NOTELF",
    11: "AT_UID",
    12: "AT_EUID",
    13: "AT_GID",
    14: "AT_EGID",
    15: "AT_CLKTCK",
    16: "AT_PLATFORM",  # usually a pointer to a NUL-terminated string
    17: "AT_HWCAP",
    18: "AT_FPUCW",
    19: "AT_DCACHEBSIZE",
    20: "AT_ICACHEBSIZE",
    21: "AT_UCACHEBSIZE",
    22: "AT_IGNOREPPC",
    23: "AT_SECURE",
    24: "AT_BASE_PLATFORM",
    25: "AT_RANDOM",  # pointer to 16 random bytes
    26: "AT_HWCAP2",
    31: "AT_EXECFN",  # pointer to program path string
    32: "AT_SYSINFO",
    33: "AT_SYSINFO_EHDR",
    34: "AT_L1I_CACHESHAPE",
    35: "AT_L1D_CACHESHAPE",
    36: "AT_L2_CACHESHAPE",
    37: "AT_L3_CACHESHAPE",
}

# ---- Selected HWCAP bits for aarch64 (from arch/arm64/include/uapi/asm/hwcap.h) ----
AARCH64_HWCAP = {
    0: "FP",
    1: "ASIMD",  # NEON
    2: "EVTSTRM",
    3: "AES",
    4: "PMULL",
    5: "SHA1",
    6: "SHA2",
    7: "CRC32",
    8: "ATOMICS",
    9: "FPHP",
    10: "ASIMDHP",
    11: "CPUID",
    12: "ASIMDRDM",
    13: "JSCVT",
    14: "FCMA",
    15: "LRCPC",
    16: "DCPOP",
    17: "SHA3",
    18: "SM3",
    19: "SM4",
    20: "ASIMDDP",
    21: "SHA512",
    22: "SVE",
    23: "ASIMDFHM",
    28: "ILRCPC",
    29: "FLAGM",
    30: "SSBS",
    31: "SB",
}

AARCH64_HWCAP2 = {
    0: "DCPODP",
    1: "SVE2",
    2: "SVEAES",
    3: "SVEPMULL",
    4: "SVEBITPERM",
    5: "SVESHA3",
    6: "SVESM4",
    7: "FLAGM2",
    8: "FRINT",
    9: "SVEI8MM",
    10: "SVEF32MM",
    11: "SVEF64MM",
    12: "SVEBF16",
    13: "I8MM",
    14: "BF16",
    15: "DGH",
    16: "RNG",
    17: "BTI",
    20: "MTE",
    21: "ECV",
    22: "AFP",
    23: "RPRFM",
    28: "MTE3",
    29: "SSELECT",
    30: "IDST",
}

# ---- Selected HWCAP bits for x86 (from arch/x86/include/uapi/asm/hwcap.h) ----
# Note: x86 typically uses CPUID directly; AT_HWCAP on x86 is sparse.
X86_HWCAP = {
    0: "FPU",
    1: "VME",
    2: "DE",
    3: "PSE",
    4: "TSC",
    5: "MSR",
    6: "PAE",
    7: "MCE",
    8: "CX8",
    9: "APIC",
    11: "SEP",
    12: "MTRR",
    13: "PGE",
    14: "MCA",
    15: "CMOV",
    16: "PAT",
    17: "PSE36",
    19: "CLFLUSH",
    23: "MMX",
    25: "SSE",
    26: "SSE2",
}


def read_string_from_maps(pid, addr, maxlen=4096):
    """
    Best-effort read of a NUL-terminated string at user address 'addr' by
    reading from /proc/<pid>/mem. Requires permission; may fail.
    """
    mem_path = f"/proc/{pid}/mem"
    try:
        with open(mem_path, "rb", buffering=0) as mem:
            mem.seek(addr)
            data = mem.read(maxlen)
            end = data.find(b"\x00")
            if end != -1:
                return data[:end].decode(errors="replace")
    except Exception:
        pass
    return None


def bits_to_names(bits, table):
    return [name for bit, name in table.items() if (bits >> bit) & 1]


def decode_hwcap(arch, hwcap, hwcap2):
    lines = []

    if arch == "aarch64":
        names1 = bits_to_names(hwcap, AARCH64_HWCAP)
        names2 = bits_to_names(hwcap2, AARCH64_HWCAP2)
        if names1:
            lines.append("HWCAP:  " + ", ".join(sorted(names1)))
        if names2:
            lines.append("HWCAP2: " + ", ".join(sorted(names2)))
    elif arch in ("x86_64", "i386", "i686"):
        names = [name for bit, name in X86_HWCAP.items() if (hwcap >> bit) & 1]
        if names:
            lines.append("HWCAP:  " + ", ".join(sorted(names)))
        if hwcap2:
            lines.append("HWCAP2: 0x%x (rarely used on x86)" % hwcap2)
    return lines


def parse_auxv(path_or_data, arch=None):
    """
    Parse auxv from either a file path or raw bytes.

    Args:
        path_or_data: Either a file path (str) or raw bytes
        arch: Optional architecture hint ('aarch64', 'x86_64', etc.)
              Used to determine word size when parsing from file

    Returns:
        List of (type, value) tuples
    """
    if isinstance(path_or_data, str):
        with open(path_or_data, "rb") as f:
            data = f.read()
    else:
        data = path_or_data

    # Determine word size
    # For self mode, use native size
    # For file mode with arch hint, use arch-specific size
    if arch == "aarch64" or arch == "x86_64":
        ul_size = 8
    elif arch in ("i386", "i686"):
        ul_size = 4
    else:
        # Default to native size
        ul_size = ctypes.sizeof(ctypes.c_ulong)

    if ul_size == 8:
        fmt = "QQ"
        step = 16
    elif ul_size == 4:
        fmt = "II"
        step = 8
    else:
        raise RuntimeError("Unsupported word size: %d" % ul_size)

    entries = []
    for i in range(0, len(data), step):
        chunk = data[i : i + step]
        if len(chunk) < step:
            break
        a_type, a_val = struct.unpack(fmt, chunk)
        if a_type == 0:  # AT_NULL
            break
        entries.append((a_type, a_val))
    return entries


def main():
    ap = argparse.ArgumentParser(
        description="Pretty-print Linux auxv",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # File mode: decode auxv from a file
  %(prog)s --decode-auxv auxv.bin --decode-arch aarch64 --decode-hwcap

  # Self mode: decode current process
  %(prog)s --decode-hwcap --try-strings
        """,
    )

    # Mode selection
    ap.add_argument(
        "--decode-auxv",
        metavar="FILE",
        help="Read auxv from binary file (file mode)",
    )
    ap.add_argument(
        "--decode-arch",
        metavar="ARCH",
        choices=SUPPORTED_ARCH_LIST,
        help="Architecture hint for file mode (aarch64, x86_64, i386, i686)",
    )

    # Options for both modes
    ap.add_argument(
        "--decode-hwcap",
        action="store_true",
        help="Decode HWCAP/HWCAP2 for known arches",
    )

    # Self mode only
    ap.add_argument(
        "--try-strings",
        action="store_true",
        help="Attempt to read strings for AT_PLATFORM/AT_EXECFN (self mode only)",
    )

    args = ap.parse_args()

    # Determine mode
    file_mode = args.decode_auxv is not None

    if file_mode:
        # File mode: read from file
        arch = args.decode_arch if args.decode_arch else platform.machine()
        entries = parse_auxv(args.decode_auxv, arch=arch)
        source_desc = f"File: {args.decode_auxv} (arch={arch})"
        str_cache = {}  # No string reading in file mode

        if args.try_strings:
            print("Warning: --try-strings ignored in file mode", file=sys.stderr)
    else:
        # Self mode: read from /proc/self/auxv
        pid = os.getpid()
        arch = platform.machine()
        auxv_path = "/proc/self/auxv"
        entries = parse_auxv(auxv_path, arch=arch)
        source_desc = f"PID {pid} (arch={arch})"

        # Optional: try to read strings for some pointer-valued tags
        str_cache = {}
        if args.try_strings:
            ptr_tags = {16, 24, 31}  # AT_PLATFORM, AT_BASE_PLATFORM, AT_EXECFN
            for t, v in entries:
                if t in ptr_tags and v:
                    s = read_string_from_maps(pid, v)
                    if s:
                        str_cache[t] = s

    # Print table
    if entries:
        name_w = max(len(AT_NAMES.get(t, "AT_%d" % t)) for t, _ in entries)
    else:
        name_w = 15

    print(f"auxv from {source_desc}:")
    for t, v in entries:
        name = AT_NAMES.get(t, "AT_%d" % t)
        if t in (16, 24, 31) and t in str_cache:
            print(f'{name:<{name_w}} : 0x{v:x}  "{str_cache[t]}"')
        elif t == 25:  # AT_RANDOM -> pointer to 16 random bytes
            print(f"{name:<{name_w}} : 0x{v:x}  (pointer to 16 random bytes)")
        else:
            # Show decimal for small scalar fields, hex for pointers/flags
            if name in (
                "AT_PHENT",
                "AT_PHNUM",
                "AT_PAGESZ",
                "AT_CLKTCK",
                "AT_UID",
                "AT_EUID",
                "AT_GID",
                "AT_EGID",
                "AT_SECURE",
            ):
                print(f"{name:<{name_w}} : {v}")
            else:
                print(f"{name:<{name_w}} : 0x{v:x}")

    if args.decode_hwcap:
        # Grab values if present
        hw1 = next((v for t, v in entries if t == 17), 0)
        hw2 = next((v for t, v in entries if t == 26), 0)
        lines = decode_hwcap(arch, hw1, hw2)
        if lines:
            print("\nDecoded HWCAP:")
            for ln in lines:
                print("  " + ln)
        else:
            print("\nDecoded HWCAP: (no known mapping for arch=%s)" % arch)


if __name__ == "__main__":
    main()
