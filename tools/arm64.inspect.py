#!/usr/bin/env python3
"""
ARM64 Binary Optimization Inspector

Analyzes ARM64 Android binaries to determine what SIMD optimizations
they contain (NEON, DOTPROD, SVE/SVE2).

Script runs llvm-objdump, and summarizes the output.

Usage:
    ./arm64.inspect.py <binary-path>
    ./arm64.inspect.py ../install/bin/x265
    ./arm64.inspect.py --llvm-objdump /path/to/llvm-objdump ../install/bin/x265
"""

import sys
import os
import subprocess
import re
import argparse
import shutil
import glob
import traceback
from pathlib import Path
from collections import OrderedDict


# ARM64 SIMD extensions to check
# Each entry: key is the output name, value is the regex pattern
INSPECT_ARRAY = {
    # 1. Core architecture and SIMD
    "asimd": r"\s(fadd|fmul|ld1|st1|saddl|uaddl|sqadd|movi|add\s+v|sub\s+v|mul\s+v|ld[234]|st[234]|umull|smull)",
    "asimdhp": r"\s(fadd\s+h|fmul\s+h|fmla\s+h|fabs\s+h|fneg\s+h|fsqrt\s+h)",
    "asimdfhm": r"\s(fmlal|fmlsl)",
    "asimddp": r"\s(sdot|udot)",
    "asimdrdm": r"\s(sqrdmlah|sqrdmlsh)",
    "i8mm": r"\s(smmla|ummla|usmmla)",
    "bf16": r"\s(bfdot|bfmmla|bfcvt)",
    "bf16fml": r"\s(bfmlalb|bfmlalt)",
    "sve": r"\sz[0-9]+\.",
    "sve2": r"\s(sqrdmlah\s+z|sqrdmlsh\s+z|match|nmatch|histcnt|histseg|addhnb|raddhnb)",
    "svei8mm": r"\s(smmla\s+z|ummla\s+z|usmmla\s+z)",
    "svebf16": r"\s(bfdot\s+z|bfmmla\s+z|bfcvt\s+z)",
    "sveaes": r"\s(aesd\s+z|aese\s+z|aesimc\s+z|aesmc\s+z)",
    "svepmull": r"\s(pmull\s+z)",
    "svebitperm": r"\s(bext|bdep|bgrp)",
    "svesha3": r"\s(rax1|bcax|eor3|xar)",
    "svesm4": r"\s(sm4e\s+z|sm4ekey\s+z)",
    "fphp": r"\s(fcvt\s+h|fadd\s+h|fmul\s+h|fdiv\s+h|fsub\s+h)",
    "fcma": r"\s(fcmla|fcadd)",
    "frint": r"\s(frint32x|frint64x|frint32z|frint64z)",

    # 2. Cryptography and hash extensions
    "aes": r"\s(aese|aesd|aesmc|aesimc)",
    "pmull": r"\s(pmull)",
    "sha1": r"\s(sha1[cpmhsu])",
    "sha2": r"\s(sha256[hsu]|sha512[hsu])",
    "sha3": r"\s(rax1|bcax|eor3|xar)",
    "sha512": r"\s(sha512[hsu])",
    "sm3": r"\s(sm3ss1|sm3tt[12][ab]|sm3partw[12])",
    "sm4": r"\s(sm4e|sm4ekey)",

    # 3. Memory, atomics, and synchronization
    "atomics": r"\s(ldadd|ldclr|ldeor|ldset|ldsmax|ldsmin|ldumax|ldumin|cas|casp|swp)",
    "dcpop": r"\sdc\s+cvap",
    "dcpodp": r"\sdc\s+cvadp",
    "uscat": r"\s(stset|stclr)",
    "lrcpc": r"\s(ldapr|stlr)",
    "ilrcpc": r"\s(ldapur|stlur)",
    "dgh": r"\sdgh",
    "sb": r"\ssb",
    "wfxt": r"\s(wfet|wfit)",

    # 4. Security and control-flow
    "paca": r"\s(pacia|pacib|autia|autib)",
    "pacg": r"\s(pacda|pacdb|pacga|autda|autdb|autga)",
    "bti": r"\sbti",
    "flagm": r"\s(setf8|setf16|rmif)",
    "flagm2": r"\s(axflag|xaflag)",

    # 5. Data conversion and integer extensions
    "jscvt": r"\sfjcvtzs",
    "crc32": r"\scrc32[bhwxc]",
    "dit": r"\smsr\s+dit",
    "cpuid": r"\smrs\s+.*,\s*id_",
    "evtstrm": r"\sevtstrm",
}


def find_llvm_objdump(user_path=None):
    """Find llvm-objdump in PATH or use user-specified path"""
    if user_path:
        # User specified a path
        objdump_path = Path(user_path)
        if not objdump_path.exists():
            raise FileNotFoundError(f"llvm-objdump not found at: {user_path}")
        return objdump_path

    # Try to find in PATH
    objdump_in_path = shutil.which("llvm-objdump")
    if objdump_in_path:
        return Path(objdump_in_path)

    # Try common Android NDK locations
    ndk_paths = [
        "/opt/android_sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump",
        os.path.expanduser(
            "~/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump"
        ),
        os.path.expanduser(
            "~/android-ndk-*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump"
        ),
    ]

    for pattern in ndk_paths:
        matches = glob.glob(pattern)
        if matches:
            # Return the first (likely newest) match
            matches.sort(reverse=True)
            return Path(matches[0])

    raise FileNotFoundError(
        "llvm-objdump not found in PATH or common NDK locations. "
        "Please specify with --llvm-objdump <path>"
    )


class BinaryInspector:
    """Inspects ARM64 binaries for SIMD optimizations"""

    def __init__(self, binary_path, llvm_objdump_path):
        self.binary_path = Path(binary_path)
        if not self.binary_path.exists():
            raise FileNotFoundError(f"Binary not found: {binary_path}")

        self.llvm_objdump = llvm_objdump_path

    def get_file_info(self):
        """Get basic file information"""
        try:
            result = subprocess.run(
                ["file", str(self.binary_path)],
                capture_output=True,
                text=True,
                check=True,
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            return f"Error: {e}"

    def get_file_size(self):
        """Get file size in human-readable format"""
        size = self.binary_path.stat().st_size
        for unit in ["B", "KB", "MB", "GB"]:
            if size < 1024.0:
                return f"{size:.1f} {unit}"
            size /= 1024.0
        return f"{size:.1f} TB"

    def get_symbols(self, pattern):
        """Get symbols matching a pattern"""
        try:
            result = subprocess.run(
                ["nm", str(self.binary_path)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode != 0:
                return []

            symbols = []
            for line in result.stdout.split("\n"):
                if re.search(pattern, line, re.IGNORECASE):
                    symbols.append(line.strip())
            return symbols
        except Exception as e:
            return []

    def disassemble(self):
        """Disassemble the binary"""
        try:
            result = subprocess.run(
                [str(self.llvm_objdump), "-d", str(self.binary_path)],
                capture_output=True,
                text=True,
                check=True,
            )
            return result.stdout
        except subprocess.CalledProcessError as e:
            return ""

    def count_instructions(self, disasm, pattern):
        """Count instructions matching a pattern in disassembly"""
        count = 0
        for line in disasm.split("\n"):
            if re.search(r"^\s+[0-9a-f]+:", line):
                if re.search(pattern, line):
                    count += 1
        return count

    def find_sample_instructions(self, disasm, pattern, limit=5):
        """Find sample instructions matching a pattern"""
        samples = []
        for line in disasm.split("\n"):
            if re.search(r"^\s+[0-9a-f]+:", line):
                if re.search(pattern, line):
                    samples.append(line.strip())
                    if len(samples) >= limit:
                        break
        return samples

    def get_build_id(self):
        """Extract build ID from binary"""
        try:
            result = subprocess.run(
                ["readelf", "-n", str(self.binary_path)],
                capture_output=True,
                text=True,
                check=False,
            )
            for line in result.stdout.split("\n"):
                if "Build ID" in line or "BuildID" in line:
                    return line.strip()
            return None
        except Exception:
            return None

    def get_embedded_strings(self, pattern):
        """Get embedded strings matching a pattern"""
        try:
            result = subprocess.run(
                ["strings", str(self.binary_path)],
                capture_output=True,
                text=True,
                check=True,
            )
            matches = []
            for line in result.stdout.split("\n"):
                if re.search(pattern, line, re.IGNORECASE):
                    matches.append(line.strip())
            return matches[:10]  # Limit to 10 results
        except Exception:
            return []

    def parse_file_info(self, file_info):
        """Parse file command output into structured fields"""
        # Remove the filename prefix (e.g., "path/to/binary: ")
        if ":" in file_info:
            file_info = file_info.split(":", 1)[1].strip()

        # Parse binary format
        binary_format_match = re.match(r"(ELF [^,]+)", file_info)
        binary_format = (
            binary_format_match.group(1) if binary_format_match else "unknown"
        )

        # Parse architecture
        arch_match = re.search(r"(ARM aarch64|x86-64|i386|MIPS)", file_info)
        arch = arch_match.group(1) if arch_match else "unknown"

        # Parse ELF version
        version_match = re.search(r"(version \d+ \([^)]+\))", file_info)
        elf_version = version_match.group(1) if version_match else "unknown"

        # Check if dynamically linked
        dynamically_linked = "dynamically linked" in file_info

        # Parse interpreter
        interpreter_match = re.search(r"interpreter ([^,]+)", file_info)
        interpreter = (
            interpreter_match.group(1).strip() if interpreter_match else "none"
        )

        # Parse Android API level
        android_match = re.search(r"for (Android \d+)", file_info)
        android_api = android_match.group(1) if android_match else "none"

        # Parse builder (NDK)
        builder_match = re.search(r"built by (NDK [^,]+)", file_info)
        builder = builder_match.group(1) if builder_match else "unknown"

        # Parse BuildID
        buildid_match = re.search(r"BuildID\[sha1\]=([0-9a-f]+)", file_info)
        build_id_sha1 = buildid_match.group(1) if buildid_match else "none"

        # Check for debug info
        debug_info = "with debug_info" in file_info

        # Check if stripped
        stripped = "stripped" in file_info and "not stripped" not in file_info

        return {
            "binary_format": binary_format,
            "arch": arch,
            "elf_version": elf_version,
            "dynamically_linked": dynamically_linked,
            "interpreter": interpreter,
            "android_api": android_api,
            "builder": builder,
            "build_id_sha1": build_id_sha1,
            "debug_info": debug_info,
            "stripped": stripped,
        }

    def inspect(self):
        """Run full inspection and return ordered dictionary of results"""
        # Get basic info
        file_info = self.get_file_info()
        parsed_info = self.parse_file_info(file_info)

        # Get file size in MB
        size_bytes = self.binary_path.stat().st_size
        size_mb = size_bytes / (1024 * 1024)

        # Disassemble and count instructions
        disasm = self.disassemble()
        total_insn = self.count_instructions(disasm, r".")

        # Count instructions for each extension using INSPECT_ARRAY
        detected = []
        undetected = []
        extension_results = OrderedDict()

        for ext_name, pattern in INSPECT_ARRAY.items():
            count = self.count_instructions(disasm, pattern)
            percentage = (count * 100.0) / total_insn if total_insn > 0 else 0.0

            extension_results[f"{ext_name}_instructions"] = count
            extension_results[f"{ext_name}_instructions_percentage"] = percentage

            # Build detected/undetected lists
            if count > 0:
                detected.append(ext_name)
            else:
                undetected.append(ext_name)

        detected_extensions = ";".join(detected) if detected else "none"
        undetected_extensions = ";".join(undetected) if undetected else "none"

        # Build final ordered dictionary
        output = OrderedDict()
        output["binary_name"] = self.binary_path.name
        output["detected_extensions"] = detected_extensions
        output["undetected_extensions"] = undetected_extensions
        output["binary_format"] = parsed_info["binary_format"]
        output["arch"] = parsed_info["arch"]
        output["elf_version"] = parsed_info["elf_version"]
        output["dynamically_linked"] = str(parsed_info["dynamically_linked"]).lower()
        output["interpreter"] = parsed_info["interpreter"]
        output["android_api"] = parsed_info["android_api"]
        output["builder"] = parsed_info["builder"]
        output["build_id_sha1"] = parsed_info["build_id_sha1"]
        output["debug_info"] = str(parsed_info["debug_info"]).lower()
        output["stripped"] = str(parsed_info["stripped"]).lower()
        output["binary_size_mb"] = size_mb
        output["total_instructions"] = total_insn

        # Add extension results
        output.update(extension_results)

        return output


def main():
    parser = argparse.ArgumentParser(
        description="ARM64 Binary Optimization Inspector - Analyzes SIMD optimizations in ARM64 binaries",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s ../install/bin/x265
  %(prog)s --llvm-objdump /path/to/llvm-objdump ../install/bin/SvtAv1EncApp
        """,
    )

    parser.add_argument("binary", help="Path to the ARM64 binary to inspect")

    parser.add_argument(
        "--llvm-objdump",
        metavar="PATH",
        help="Path to llvm-objdump (auto-detected if not specified)",
    )

    parser.add_argument(
        "-d",
        "--debug",
        action="count",
        default=0,
        help="Increase debug level (can be used multiple times, -d, -dd, etc.)",
    )

    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Quiet mode - only show essential information (debug level 0)",
    )

    args = parser.parse_args()

    # Handle debug level
    debug_level = 0 if args.quiet else args.debug

    try:
        # Find llvm-objdump
        llvm_objdump = find_llvm_objdump(args.llvm_objdump)

        # Inspect binary
        inspector = BinaryInspector(args.binary, llvm_objdump)
        results = inspector.inspect()

        # Print results
        for key, value in results.items():
            # Skip instruction counts and percentages if debug level < 1
            if debug_level < 1 and (
                key.endswith("_instructions") or key.endswith("_instructions_percentage")
            ):
                continue

            # Format percentage and size values
            if key.endswith("_percentage"):
                print(f"{key}: {value:.2f}")
            elif key == "binary_size_mb":
                print(f"{key}: {value:.1f}")
            else:
                print(f"{key}: {value}")
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
