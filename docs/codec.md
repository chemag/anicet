# Codec Configuration Parameters

This document describes the encoder configuration parameters for each codec supported by anicet.


# 1. Codec Configuration mechanism

Anicet has a mechanism for encoders to advertise the configuration parameters they support. For
example, the x265 encoder supports the following parameters:
* "optimization": {"opt", "nonopt"},
* "preset": {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"},
* "tune": {"psnr", "ssim", "grain", "zerolatency", "fastdecode"},
* "rate-control": {"crf", "cqp", "abr", "cbr", "2-pass"}
* and 3x numeric parameters, namely "bitrate", "qp", and "crf"

We allow the user to set parameters in 2 ways:
* (1) using CLI options.
* (2) using a JSON config file.


# 1.1. CLI-Based Codec Configuration

The mechanism for the CLI options looks like this:
```
$ ./anicet [input image info] --codec x265 --num-runs 2 --x265 optimization=opt,preset=ultrafast,tune=psnr,rate-control=cqp,qp=30
```

Notes:
* We want to add getopt glue that supports this type of extended options, and
  converts them into `setup.parameter_map[key] = value` entries.
* We want to tell the user when an option (key) is unknown, when a value is
  invalid, and also support "--x265-8bit help" that lists all the x265-8bit
  options.


# 1.2. JSON-Based Codec Configuration

Anicet allows the user to provide a JSON  file similar to what she is getting in the output json file.

Right now the output json files looks as follows:
```
{
  "input": {
    ... input info
  },
  "setup": {
    "serial_number": "...",
    "codec": "x265",
    "num_runs": 2
  },
  "output": {
    "frames": [
       ... per-frame info
    ]
  },
  "resources": {
    "global": {
      ...
        },
    "frames": [
      ...
  }
}
```

We want to allow the user to provide the same syntax than in CLI case, but using a json file.
```
$ ./anicet [input image info] --config setup.json

$ cat setup.json
{
  "serial_number": "...",
  "codec": "x265",
  "num_runs": 2
  "x265": {
    "optimization": "opt",
    "preset": "ultrafast",
    "tune": "psnr",
    "rate-control": "cqp",
    "qp": 30
  }
}
```

Notes:
* The second approach will allow shorter commands. Note that some commands make more sense
  in the CLI (e.g. the "`serial_number`" field). CLI parameters should have priority over
  config values.
* Also, at some point using lists instead of values. I.e. at some point I could run this setup.json:

```
$ cat setup.json
{
  "codec": "x265",
  "num_runs": 2
  "x265": {
    "optimization": ["opt", "nonopt"],
    "preset": ["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"],
    "tune": ["psnr", "ssim", "grain", "zerolatency", "fastdecode"],
    "rate-control": ["crf", "cqp", "abr", "cbr", "2-pass"]
    "qp": 30,
    "bitrate": 1000000
  }
}
```


# 2. x265 (H.265/HEVC)

Variants
* `x265`: Optimized build with NEON/assembly optimizations
* `x265nonopt`: Non-optimized build without SIMD optimizations

Parameters
* **Preset**: `"medium"`: Encoding speed/quality trade-off
  - Available options: ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow, placebo
  - Higher presets = slower encoding, better compression
* **Tune**: `"zerolatency"`: Optimized for low-latency encoding
  - Available options: psnr, ssim, grain, zerolatency, fastdecode
* **Rate Control Mode**: CRF (Constant Rate Factor): Currently used
  - Not explicitly set in code, uses x265 default (CRF 28)
  - Lower CRF = higher quality (range: 0-51, typical 18-28)
  - **Available rate control modes:**
    - **CRF** (Constant Rate Factor): Quality-based, variable bitrate (default)
    - **CQP** (Constant Quantizer): Constant QP, variable bitrate
    - **ABR** (Average Bitrate): Target average bitrate over entire encode
    - **CBR** (Constant Bitrate): Constant bitrate, requires VBV buffer
    - **2-pass**: Two-pass VBR for optimal bitrate distribution
  - **Current implementation:** Uses default CRF mode (quality-based encoding)
  - **To use other modes (not currently implemented):**
    - CBR: Set `param->rc.rateControlMode = X265_RC_CBR` and `param->rc.bitrate`
    - ABR: Set `param->rc.rateControlMode = X265_RC_ABR` and `param->rc.bitrate`
    - CQP: Set `param->rc.qp` for constant quantizer
* **Bit depth**: 8-bit
* **Frame type**: I-frame only (intra-only encoding)
  - `keyframeMax = 1`, `bframes = 0`
* **Frame rate**: 30 fps
* **Color space**: YUV420p (4:2:0 chroma subsampling)


# 3. SVT-AV1 (AV1)

Parameters
* **Preset**: Default (not explicitly set)
  - Uses SVT-AV1 library default (typically preset 8)
  - Range: 0-13, where 0=slowest/best quality, 13=fastest/lower quality
  - Preset 8 is a balanced medium speed setting
* **Rate Control Mode**: CRF (Constant Rate Factor): Currently used
  - Not explicitly set in code, uses SVT-AV1 default (CRF 35)
  - Lower CRF = higher quality (range: 0-63)
  - **Available rate control modes:**
    - **CRF** (Constant Rate Factor): Quality-based, variable bitrate (default)
    - **CQP** (Constant Quantizer): Constant QP, variable bitrate
    - **VBR** (Variable Bitrate): Target bitrate with quality variation
    - **CBR** (Constant Bitrate): Constant bitrate output
  - **Current implementation:** Uses default CRF mode (quality-based encoding)
  - **To use other modes (not currently implemented):**
    - CRF: Set `config.rate_control_mode = SVT_AV1_RC_MODE_CRF` and `config.qp` or `config.crf`
    - CQP: Set `config.rate_control_mode = SVT_AV1_RC_MODE_CQP` and `config.qp`
    - VBR: Set `config.rate_control_mode = SVT_AV1_RC_MODE_VBR` and `config.target_bit_rate`
    - CBR: Set `config.rate_control_mode = SVT_AV1_RC_MODE_CBR` and `config.target_bit_rate`
* **Tune**: `"PSNR"` (default from library)
  - Available options: PSNR (default), subjective (VQ)
* **Bit depth**: 8-bit
* **Frame type**: Key frames only
  - `intra_period_length = -1` (every frame is a key frame)
  - `intra_refresh_type = SVT_AV1_KF_REFRESH`
* **Frame rate**: 30 fps
* **Color space**: YUV420 (4:2:0 chroma subsampling)
* **Prediction structure**: Random access (default)
* **SIMD Control**: `use_cpu_flags` (EbCpuFlags): Currently not set
  - Type: uint64_t bitmask
  - **ARM flags** (defined in `EbSvtAv1.h`):
    - `EB_CPU_FLAGS_NEON`: Armv8.0-A mandatory NEON instructions
    - `EB_CPU_FLAGS_ARM_CRC32`: Armv8.0-A optional CRC32 (mandatory from Armv8.1-A)
    - `EB_CPU_FLAGS_NEON_DOTPROD`: Armv8.2-A optional dot-product (mandatory from Armv8.4-A)
    - `EB_CPU_FLAGS_NEON_I8MM`: Armv8.2-A optional i8mm (mandatory from Armv8.6-A)
    - `EB_CPU_FLAGS_SVE`: Armv8.2-A optional SVE (mandatory from Armv9.0-A)
    - `EB_CPU_FLAGS_SVE2`: Armv9.0-A SVE2 instructions
  - **x86 flags**: SSE, SSE2, SSE3, SSSE3, SSE4_1, SSE4_2, AVX, AVX2, AVX512F, AVX512ICL, etc.
  - **Current implementation:** Uses all available CPU features (default auto-detect)
  - **To restrict SIMD (not currently implemented):** Set `config.use_cpu_flags` to specific flag combination
  - Example: `config.use_cpu_flags = EB_CPU_FLAGS_NEON;` (disable SVE/SVE2, use only NEON)


Notes:
* SVT-AV1 verbose output is suppressed when debug_level <= 1
* Uses NEON/SVE2 optimizations on ARM64 when available
* Multi-threaded encoding with automatic thread pool sizing


# 4. libjpeg-turbo (JPEG)

Variants
* `libjpeg-turbo`: Optimized build with SIMD optimizations (NEON on ARM)
* `libjpeg-turbo-nonopt`: Non-optimized build without SIMD

Parameters
* **Quality**: 75
  - Range: 0-100, where 100 = highest quality, minimal compression
  - 75 is typically considered "high quality" for JPEG
* **DCT Method**:
  - Optimized: `TJFLAG_FASTDCT` (fast discrete cosine transform)
  - Non-optimized: Slower accurate DCT
* **Subsampling**: `TJSAMP_420` (4:2:0 chroma subsampling)
  - Matches YUV420p input format
* **Color space**: YCbCr (converted from YUV420p input)


Notes
* Uses TurboJPEG API for direct YUV420 compression
* No intermediate RGB conversion (efficient)
* Output buffer is dynamically allocated by the library



# 5. jpegli (JPEG XL's JPEG encoder)

Parameters
* **Quality**: 75
  - Range: 0-100, same scale as libjpeg
  - jpegli typically achieves better compression than libjpeg at same quality level
* **Color space**: YCbCr
  - Direct YUV420p input to avoid color space conversion
* **Sampling factors**: 4:2:0 (YUV420p)
  - Y component: 2x2 sampling (full resolution)
  - U component: 1x1 sampling (half resolution)
  - V component: 1x1 sampling (half resolution)
* **DCT block size**: 8x8 (standard JPEG)
* **Raw data mode**: Enabled (`raw_data_in = TRUE`)
* **SIMD Control**: Google Highway library: Currently not configured
  - Uses Highway's dynamic dispatch (auto-selects best SIMD at runtime)
  - **Runtime control (not currently implemented):**
    - Disable targets: `hwy::DisableTargets(int64_t disabled_targets)`
    - Set specific targets: `hwy::SetSupportedTargetsForTest(int64_t targets)`
  - **Compile-time control (build configuration):**
    - Disable all SIMD: Define `HWY_COMPILE_ONLY_SCALAR`
    - Disable specific targets: Define `HWY_DISABLED_TARGETS` bitmask
  - **ARM Highway targets** (defined in `highway/hwy/targets.h`):
    - `HWY_NEON`: ARM NEON (baseline)
    - `HWY_NEON_BF16`: NEON with BFloat16
    - `HWY_SVE`: ARM SVE (Scalable Vector Extension)
    - `HWY_SVE2`: ARM SVE2
    - `HWY_SVE_256`: SVE with 256-bit vectors
    - `HWY_SVE2_128`: SVE2 with 128-bit vectors
  - **x86 Highway targets**: HWY_SSE2, HWY_SSSE3, HWY_SSE4, HWY_AVX2, HWY_AVX3, HWY_AVX3_DL, etc.
  - **Fallback targets**: HWY_EMU128 (128-bit emulation), HWY_SCALAR (no SIMD)
  - **Current implementation:** Uses Highway default auto-detection

Notes
* Uses libjxl's jpegli library
* Produces valid JPEG files compatible with all JPEG decoders
* Generally better compression efficiency than libjpeg-turbo at same quality


# 6. WebP

Variants
* `webp`: Optimized build with SIMD optimizations
* `webp-nonopt`: Non-optimized build without SIMD

Parameters
* **Quality**: 75
  - Range: 0-100 (lossy compression)
  - 100 = lossless compression
* **Method**: 4 (speed/quality trade-off)
  - Range: 0-6
  - 0 = fastest, 6 = slowest/best compression
  - 4 = balanced medium speed
* **Color space**: YUV420
  - Direct YUV420p input (`use_argb = 0`)
  - Avoids RGB conversion overhead

Notes
* Both opt and nonopt variants use same quality/method parameters
* Optimization difference is in SIMD instruction usage
* WebP typically achieves better compression than JPEG at same quality


# 7. MediaCodec (Android Hardware Encoder)

Parameters
* **Codec name**: `"c2.android.hevc.encoder"` (HEVC/H.265)
  - Hardcoded in `anicet_runner.cc` line 447
  - Uses Android Codec2 (C2) API
  - Typically maps to hardware encoder (e.g., Qualcomm, MediaTek)
* **Quality**: 75
  - Codec-specific quality scale
* **Bitrate**: Auto-calculated from quality (`-1`)
  - MediaCodec API calculates bitrate internally based on resolution and quality
* **Color format**: YUV420p (NV12 or similar, device-dependent)
* **I-frame interval**: Default (codec-dependent)
* **Profile/Level**: Auto-selected based on resolution


Notes
* Only available on Android (`#ifdef __ANDROID__`)
* Uses hardware encoder when available (device-dependent)
* Encoder capabilities vary by device/SoC
* Default uses HEVC (H.265), not AVC (H.264)
* Alternative codec names (device-dependent):
  - `c2.android.avc.encoder` (H.264)
  - `c2.android.hevc.encoder` (H.265, default)
  - `c2.qti.hevc.encoder` (Qualcomm HEVC)
  - `c2.mtk.hevc.encoder` (MediaTek HEVC)
