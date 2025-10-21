# ANICET: Android Native Image Codec Evaluation Tool

Anicet is a lightweight C++ wrapper to measure resource utilization for binaries on Android. The goal is to use it to measure the complexity, size, and quality of image encoders in Android devices.

The suite includes both software encoders (x265, SVT-AV1, libjpeg-turbo, jpegli, JXS, libwebp) and a hardware encoder wrapper (android-mediacodec) for testing device-specific hardware accelerators.

Anicet is a wrapper around simpleperf and other tools. It includes CPU time and peak memory usage (VmHWM). The tool also report tags, sets CPU affinity, enforces timeouts, and outputs results in either CSV or JSON.

simpleperf is used to collect performance counters (CPU cycles, instructions, cache misses) alongside resource metrics in a single command.

At some point we will add support for Perfetto heapprofd for detailed heap profiles.


# 1. Build Instructions

## Compile for Android

```
# If you have the Android NDK toolchain in PATH:
aarch64-linux-android21-clang++ -O2 -static -s -o anicet anicet.cpp
adb push anicet /data/local/tmp/
adb shell chmod +x /data/local/tmp/anicet
```

Static linking is optional. If linking statically is inconvenient, drop `-static` and ensure the device has compatible `libc++`/`bionic` libraries.


# 2. Example Usage

## Run an Encoders 10 Times and Log Results

```
OUT=/sdcard/encode_bench.csv
echo "encoder,q,threads,rep,wall_ms,user_ms,sys_ms,vmhwm_kb,exit" > $OUT

for REP in $(seq 1 10); do
  /data/local/tmp/anicet \
    --tag encoder=svtav1 --tag q=28 --tag threads=4 --tag rep=$REP \
    --cpus 4-7 --timeout-ms 600000 \
    -- /system/bin/SvtAv1EncApp -i /sdcard/in_4k.yuv -b /sdcard/out.obu \
       --keyint 1 --preset 5 --hierarchical-levels 0 --lp 4
done | awk -F, '
  BEGIN{}
  { for(i=1;i<=NF;i++){ split($i,a,"="); kv[a[1]]=a[2]; }
    print kv["encoder"]","kv["q"]","kv["threads"]","kv["rep"]","kv["wall_ms"]","kv["user_ms"]","kv["sys_ms"]","kv["vmhwm_kb"]","kv["exit"];
  }' >> $OUT
```

Swap the encoder command with equivalents for **x265**, **cjpegli**, **jxs**, **cwebp**, and **android-mediacodec**.

To output JSON instead of CSV, add `--json`.


## Testing Hardware Encoders with MediaCodec

The `android-mediacodec` tool allows testing device-specific hardware encoders (like HEIC):

```bash
# List all available encoders on the device
adb shell /data/local/tmp/android-mediacodec --list-codecs

# List only image/video encoders (HEVC, HEIC, AVC, H264, VP9, AV1)
adb shell /data/local/tmp/android-mediacodec --list-image-codecs

# Encode a single frame with hardware HEVC encoder (image mode)
adb shell /data/local/tmp/android-mediacodec \
  --codec-name c2.exynos.hevc.encoder \
  --input /sdcard/input_4k.yuv \
  --output /sdcard/output.hevc \
  --width 3840 --height 2160 \
  --quality 90 \
  --frame-count 1

# Benchmark with anicet wrapper
adb shell /data/local/tmp/anicet \
  --tag encoder=hevc_hw --tag quality=90 --tag resolution=4k \
  --cpus 4-7 --timeout-ms 60000 \
  -- /data/local/tmp/android-mediacodec \
     --codec-name c2.exynos.hevc.encoder \
     --input /sdcard/test_4k.yuv --output /sdcard/out.hevc \
     --width 3840 --height 2160 --quality 90

# Encode video with hardware HEVC encoder
adb shell /data/local/tmp/android-mediacodec \
  --codec-name c2.exynos.hevc.encoder \
  --input /sdcard/video.yuv \
  --output /sdcard/video.hevc \
  --width 1920 --height 1080 \
  --bitrate 8000000 \
  --frame-count 300 --frame-rate 30
```

The `--list-image-codecs` flag will show hardware and software encoders available on your device. Common encoder types:
- **c2.exynos.*** - Samsung Exynos hardware encoders
- **c2.qti.*** - Qualcomm hardware encoders
- **c2.android.*** - Android software encoders (fallback)

## Using Simpleperf Integration

You can now integrate simpleperf directly with `anicet` to collect performance counters without complex command nesting:

```
/data/local/tmp/anicet \
  --add-simpleperf \
  --simpleperf-command stat \
  --simpleperf-events cpu-cycles,instructions,L1-dcache-load-misses,LLC-load-misses \
  --tag encoder=x265 --tag q=28 --tag threads=4 \
  --cpus 4-7 --timeout-ms 600000 \
  -- /system/bin/x265 --input /sdcard/in.yuv --output /sdcard/out.hevc \
     --keyint 1 --crf 28 --pools 4
```

This replaces the previous approach of wrapping `anicet` with `simpleperf`, making the command simpler and ensuring all metrics are captured in a single output line.


# Features

## Key Capabilities

* Measures:

  * **Wall time** (ms)
  * **User CPU time** (ms)
  * **System CPU time** (ms)
  * **Peak memory usage** (`VmHWM`, in kB)
  * **Exit code**
* Supports **tags** (`--tag key=val`) to annotate runs
* Supports **CPU affinity** (`--cpus 4-7`)
* Supports **timeouts** (`--timeout-ms 600000`)
* Emits **CSV** (default) or **JSON** output
* **Integrated simpleperf support** (`--add-simpleperf`) to collect performance counters (CPU cycles, instructions, cache misses, etc.)

## Accuracy

* `VmHWM` is read while the child process still exists using `waitid(..., WNOWAIT)` ensuring the true peak is captured.
* The wrapper's own overhead is negligible compared to encoding time.
* CPU usage (user/sys) is derived from `wait4` and reported in milliseconds.

## Signal Handling

* The wrapper forwards `SIGINT`, `SIGTERM`, and `SIGHUP` to the child.
* Timeout kills are reported with exit code **137** (SIGKILL).



# Practical Notes

## Affinity

Use `--cpus` to pin threads to big cores for consistent benchmarking:

```bash
--cpus 4-7
```

Adjust core indices based on your SoC topology.

## Timeouts

If an encoder hangs, you can enforce limits:

```bash
--timeout-ms 600000
```

This kills the process after 10 minutes.

## Overhead

* A few syscalls and one `/proc/<pid>/status` read.
* Does **not** impact child VmHWM measurement.

## Output Examples

**CSV line (without simpleperf):**

```
encoder=svtav1,q=28,threads=4,rep=1,wall_ms=2145,user_ms=6421,sys_ms=108,vmhwm_kb=178320,exit=0
```

**CSV line (with simpleperf):**

```
encoder=x265,q=28,threads=4,wall_ms=3521,user_ms=12840,sys_ms=142,vmhwm_kb=256780,exit=0,cpu_cycles=45234567890,instructions=98765432100,L1_dcache_load_misses=123456789,LLC_load_misses=12345678
```

**JSON line (with simpleperf):**

```json
{"encoder":"x265","q":"28","threads":"4","wall_ms":3521,"user_ms":12840,"sys_ms":142,"vmhwm_kb":256780,"exit":0,"cpu_cycles":45234567890,"instructions":98765432100,"L1_dcache_load_misses":123456789,"LLC_load_misses":12345678}
```



# Integration Tips

* Always normalize metrics to **per pixel** for cross-encoder comparisons.
* Repeat runs 10x, drop first warm-up, report median.
* Run on a **cool, idle, airplane-mode** device for stable measurements.
* Use `--add-simpleperf` to collect CPU performance counters alongside resource metrics in a single command.
* For detailed heap profiling, combine with **Perfetto heapprofd**.


