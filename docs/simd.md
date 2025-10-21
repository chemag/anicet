# Assembly Optimizations in Image Encoders

This document provides a comprehensive analysis of SIMD (Single Instruction Multiple Data) and assembly optimizations across all five encoder libraries included in the anicet suite. Understanding these optimizations is critical for maximizing encoding performance on ARM64 Android devices.

Summary:
* x265 uses compile-time, hand-written baseline NEON.
* SVT-AV1 has the most advanced SIMD optimizations (DOTPROD, I8MM, SVE). Optimizations can be controlled in runtime.
* Google's jpegli uses SVE via the Google Highway library. Optimizations can be controlled in runtime.
* libjpeg-turbo uses compile-time, hand-written baseline NEON.
* libwebp uses compile-time, hand-written baseline NEON.
* jxs has no optimizations: There is some NEON code from compiler auto-vectorization (compiler converts plain C loops to NEON).


# 1. Introduction

There are 3 basic ways to use ARM SIMD extensions assembly:
* (1) Hand-written assembly
* (2) Hand-written intrinsics (kind of a generic assembly mechanism using c/c++)
* (3) Compiler auto-vectorization

Hand-written assembly consists of writing raw ARM assembly using SIMD instructions directly. It provides maximum performance, but it is compiler-specific (different syntaxes in different compilers), arch-specific (e.g. there are different AArch32 and AArch64 encodings) and hard to maintain.

Hand-written intrinsics is an easier, arch-agnostic mechanism to write SIMD instructions. By using a standard intrinsics definition (ACLE for ARM intrinsics, Intel intrinsics for x86-64), you get support for different archs for the same platform. The compiler provides a set of special functions that map one-to-one to SIMD instructions. It provides slightly less performance, but it is more portable across compilers (as long as they support ARM intrinsics) and easier to operate (regular c/c++ syntax).

A special case of hand-written intrinsics is Highway (hwy), a Google portable SIMD library that provides platform-portable SIMD intrinsics: This provides a high-level abstraction over platform-specific SIMD instructions.

Finally, compiler auto-vectorization works by having the compiler automatically convert plain scalar c/c++ loops into SIMD instructions. It is the easier mechanism,. but the less effective (it has limited use).

There are 2 approaches to add SIMD code:
* (1) compile-time SIMD: The SIMD instruction set is selected at build time. The compiler generates a specific version of the code. This means that the use of the binary is reduced to a platform that supports the instruction set built in.
* (2) run-time SIMD: Several SIMD versions are compiled and embedded into the binary. At runtime, the program detects the CPU's SIMD capabilities (e.g., SVE vs. DOTPROD), and dispatches the best available one dynamically.

Note that, from the assembler instructions only, it is hard to tell apart reliably hand-written assembly, intrinsics, or compiler code for NEON in a binary. Once compiled, all of them produce identical machine code. Some heuristics can be used to infer the optimization level:
* (1) High SIMD instruction counts (or percentage) typically point to hand-written or intrinsics. Low percentages suggests compiler auto-vectorization.
* (2) Function symbols with SIMD suffixes (e.g. "`funname_neon`" or "`funname_dotprod`") suggests assembly/intrinsics and run-time SIMD.

Checking the source code allows a better understanding of the code structure.


# 2. x265 (HEVC/H.265)

x265 contains extensive hand-written SIMD assembly, supporting 3 major families of CPU optimizations:
* (1) x86: x265 uses SSE2, SSSE3, SSE4.1, AVX, AVX2, and AVX-512. Code is located in `source/common/x86/`, and it includes several .asm files, each of which corresponds to a C reference version under `source/common/`. Performance is focused on aggressive vectorization (256- and 512-bit operations). 10- and 12-bit codecs use AVX2 or AVX-512.
* (2) ARM32: Uses NEON (aka ARM32 NEON) SIMD (128-bit) for ARMv7. Code is located in `source/common/arm/`. Can be targeted for ARMv7-A using "-mfpu=neon" or "-march=armv7-a -mfloat-abi=softfp". Performance is focused on pixel operations (SAD/SATD), motion compensation and filters, DCT/IDCT, transform steps, intra prediction, and deblocking.
* (3) AArch64: Uses Advanced NEON (aka AArch64 NEON aka ASIMD). Code is located in `source/common/aarch64/`.

Optimizations are written in GNU-style ARM assembler, and get used based on `ENABLE_ASSEMBLY` variable. Note that the ARM/AArch64 optimizations are included at compile time (the x86 optimizations are runtime detected).


# 3. SVT-AV1 (AV1)

svt-av1 supports multiple SIMD optimization architectures. It uses intrinsics in ARM NEON, and handwritten assembly everywhere else. It supoprts the major families of CPU optimizations:
(1) x86: svt-av1 uses SSE2, SSSE3, SSE4.1, AVX2, and AVX-512. Code lives under `Source/Lib/ASM_*`. Focus is aggressive vectorization (256- and 512-bit) across motion estimation (SAD/SATD), transforms, interpolation/filters, CDEF/loop filters, variance/SSE, etc.
(2) AArch64 (ARM64): svt-av1 provides ASIMD (NEON), including NEON DOTPROD and I8MM extensions, and SVE/SVE2 optimizations. Optimization is similar tot he x86 one: SAD/SATD loops (sum of absolute differences, sum of absolute transform differences), CDEF filter and loop restoration filters, motion compensation and intra prediction, transform (DCT/IDCT) and residual calculation, variance and block matching, etc.

SVT-AV1 uses run-time SIMD code: Compiler builds alternate code for different extensions, and runs them when it detects the CPU supports them.

Features are detected at build/runtime (flags like `HAVE_NEON`, `HAVE_NEON_DOTPROD`, `HAVE_NEON_I8MM`, `HAVE_SVE`/`HAVE_SVE2` show up in build configs).

Note that svt-av1 provides an "--asm <idx>" option that allows selecting the optimization level. For aarch64, this means:
* 0: C only
* 1: NEON
* 2: NEON+DOTPROD
* 3: NEON + DOTPROD + I8MM
* 4: all tiers including SVE

* max: all optimizations


# 4. jpegli (JPEG)

jpegli uses Google Highway to provide SIMD optimizations.

This means that jpegli can use NEON, DOTPROD, SVE, and SVE2 optimizations, and generates separate optimized functions that can be selected at runtime.

Highway is used in jpegli for color space conversion, DCT/IDCT transforms, quantization, Huffman encoding, and adaptive quantization.

In terms of operation, Highway detects the CPU automatically.

jpegli allows restricting its use of SIMD optimizations using highway environment variables:

* no SIMD restrictions (default - runtime dispatch)
```
$ ./jpegli-build/tools/cjpegli <input> <output>
```

* disable SVE, use only NEON
```
$ HWY_BASELINE_TARGETS=1 ./jpegli-build/tools/cjpegli <input> <output>
```

* force scalar C code only (disable all SIMD)
```
$ HWY_COMPILE_ONLY_SCALAR=1 ./jpegli-build/tools/cjpegli <input> <output>
```


# 5. libjpeg-turbo (JPEG)

libjpeg-turbo is the industry-standard JPEG codec. It includes hand-written code for different SIMD optimizations, including i386, x86-64, mips, mips64, powerpc, and ARM. The latter includes ARM64 NEON assembly optimizations (libjpeg-turbo/simd/arm/aarch64/).

NEON optimizations are used for color space conversion, downsampling/upsampling, DCT/IDCT transforms, quantization, and Huffman bit processing. Optimizations are limited to ARMv8.0 NEON.

NEON optimizations are included at compule time, and controlled using the CMake `WITH_SIMD=ON` setting.


# 6. libwebp

libwebp contains extensive hand-written SIMD assembly, supporting 3 major families of CPU optimizations:
* (1) x86: libwebp has some SSE, SSE4.1, and AVX2 files.
* (2) MIPS: libwebp has some MIPS32, MIPS DSP R2, and some MSA (MIPS SIMD Architecture) code.
* (3) ARM: Uses NEON (aka AArch64 NEON aka ASIMD). Code is located in `src/dsp/`.

Optimizations are written in GNU-style ARM assembler, and get used based on `ENABLE_ASSEMBLY` variable. Note that the ARM/AArch64 optimizations are included at compile time (the x86 optimizations are runtime detected).


# 7. JXS (JPEG XS)

JXS is the JPEG XS (ISO/IEC 21122) reference encoder/decoder. It is implemented in pure C with no SIMD optimizations. This is an intentional design choice aligned with JPEG XS's design philosophy: low-latency, low-complexity image codec (at the cost of low compression ratios).


# 8. Discussion

| Encoder       | SIMD Use                      | ARM64 extensions              | Operation    |
|---------------|-------------------------------|-------------------------------|--------------|
| x265          | Hand-written assembly         | NEON                          | compile-time |
| SVT-AV1       | C intrinsics                  | NEON, DOTPROD, I8MM, SVE/SVE2 | runtime      |
| jpegli        | Highway library               | NEON, DOTPROD, SVE, SVE2      | runtime      |
| libjpeg-turbo | Hand-written assembly         | NEON                          | compile-time |
| libwebp       | Hand-written assembly         | NEON                          | compile-time |
| JXS           | Implicit (auto-vectorization) | none                          | none         |
