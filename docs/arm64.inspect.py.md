# arm64.inspect.py: A Tool For Inspecting Binary Optimizations

This guide explains how to determine the SIMD optimizations present in an ARM64 Android binary using the arm64.inspect.py tool.


# 1. Operation

Use arm64.inspect.py to understand the structure of a binary:

```
$ tools/arm64.inspect.py [-d] <binary-path>
```

Examples:
```
$ tools/arm64.inspect.py install/bin/x265
binary_name: x265
detected_extensions: asimd;atomics;lrcpc;sb;paca;bti
undetected_extensions: asimdhp;asimdfhm;asimddp;asimdrdm;i8mm;bf16;bf16fml;sve;sve2;svei8mm;svebf16;sveaes;svepmull;svebitperm;svesha3;svesm4;fphp;fcma;frint;aes;pmull;sha1;sha2;sha3;sha512;sm3;sm4;dcpop;dcpodp;uscat;ilrcpc;dgh;wfxt;pacg;flagm;flagm2;jscvt;crc32;dit;cpuid;evtstrm
binary_format: ELF 64-bit LSB pie executable
arch: ARM aarch64
elf_version: version 1 (SYSV)
dynamically_linked: true
interpreter: /system/bin/linker64
android_api: Android 21
builder: NDK r29 (14206865)
build_id_sha1: 2d2fb28068dcdc91054fb9b63193a206a615308f
debug_info: true
stripped: false
binary_size_mb: 30.5
```

Use debug mode to count the total number of SIMD instructions:
```
$ tools/arm64.inspect.py -d install/bin/x265
binary_name: x265
detected_extensions: asimd;atomics;lrcpc;sb;paca;bti
undetected_extensions: asimdhp;asimdfhm;asimddp;asimdrdm;i8mm;bf16;bf16fml;sve;sve2;svei8mm;svebf16;sveaes;svepmull;svebitperm;svesha3;svesm4;fphp;fcma;frint;aes;pmull;sha1;sha2;sha3;sha512;sm3;sm4;dcpop;dcpodp;uscat;ilrcpc;dgh;wfxt;pacg;flagm;flagm2;jscvt;crc32;dit;cpuid;evtstrm
binary_format: ELF 64-bit LSB pie executable
arch: ARM aarch64
elf_version: version 1 (SYSV)
dynamically_linked: true
interpreter: /system/bin/linker64
android_api: Android 21
builder: NDK r29 (14206865)
build_id_sha1: 2d2fb28068dcdc91054fb9b63193a206a615308f
debug_info: true
stripped: false
binary_size_mb: 30.5
total_instructions: 732674
asimd_instructions: 76013
asimd_instructions_percentage: 10.37
...
sb_instructions: 1180
sb_instructions_percentage: 0.16
...
paca_instructions: 1249
paca_instructions_percentage: 0.17
...
bti_instructions: 618
bti_instructions_percentage: 0.08
...
```



# Appendix 1: Operation Notes

# A1.1. Check for NEON-Specific Symbols

Count NEON symbols
```
$ nm install/bin/x265 | grep -i "_neon" | wc -l
```

Show sample NEON symbols
```
$ nm install/bin/x265 | grep -i "_neon" | head -10
```

SIMD-specific functions sometimes have function names ending in `_neon`, `_asimd`, `_dotprod`, `_sve`.


## A2.2. Disassemble and Look for Specific Extension Instructions

Check for DOTPROD instructions (ARMv8.2-A).
```
$ llvm-objdump -d install/bin/SvtAv1EncApp | grep -E "\s(sdot|udot)" | wc -l
```

DOTPROD functions include:
* `sdot`: signed dot product
* `udot`: unsigned dot product


Example:
```
389124: 4e839630     sdot	v16.4s, v17.16b, v3.16b
```

Check for I8MM instructions (ARMv8.2-A+i8mm).
```
$ llvm-objdump -d install/bin/SvtAv1EncApp | grep -E "\s(smmla|ummla|usmmla)" | wc -l
```


Check for SVE instructions
```
$ llvm-objdump -d install/bin/SvtAv1EncApp | grep -E "\sz[0-9]+\." | wc -l
```

SVE functions include:
* Register names: `z0`, `z1`, `z2`, etc. (SVE vector registers)
* `p0`, `p1` (SVE predicate registers)
