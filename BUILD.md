# Anicet Build Guide

This guide explains how to build anicet and all encoder libraries for Android using CMake.


# 1. Operation Instructions

## 1.1. Clone Repository

```
$ git clone --recursive https://github.com/chemag/anicet
$ cd anicet
```

## 1.2. Build for Android

First of all, set the `ANDROID_NDK` environment variable to a specific version.
```
$ export ANDROID_NDK=/opt/android_sdk/ndk/29.0.14033849
```

Create build directory, configure for Android, and build everything
(all encoders plus the anicet wrapper).
```
$ mkdir build && cd build

$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-21 \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DCMAKE_INSTALL_PREFIX=../install \
      ..

$ cmake --build . --parallel
```

Install to staging directory
```
$ cmake --install .
```

## 1.3. Install to Android Device

Install fromthe build directory:
```
$ adb shell "mkdir -p /data/local/tmp/bin"
$ adb push ../install/bin/* /data/local/tmp/bin/
$ adb shell "chmod +x /data/local/tmp/bin/*"
```

Verify installation
```
$ adb shell /data/local/tmp/bin/anicet --help
```

## 1.4. Build Discussion

All encoder libraries are built as static binaries using CMake with the Android NDK toolchain:

1. x265 (HEVC/H.265)
- Source: `lib/x265/source`
- Build: `build/x265-build/`
- Features: CLI enabled, high bit depth support

2. SVT-AV1 (AV1)
- Source: `lib/svt-av1/`
- Build: `build/svt-av1-build/`
- Features: Encoder app, decoder disabled, testing disabled

3. jpegli (JPEG)
- Source: `lib/jpegli/`
- Build: `build/jpegli-build/`
- Features: Tools enabled, libjpeg API compatibility

4. libjpeg-turbo (JPEG)
- Source: `lib/libjpeg-turbo/`
- Build: `build/libjpeg-turbo-build/`
- Features: SIMD optimizations enabled

5. JXS (JPEG XS)
- Source: `lib/jxs/`
- Build: `build/jxs-build/`
- Features: Static library build

Each encoder is built as an ExternalProject to maintain isolation and allow independent configuration.

The anicet wrapper is built as a standard CMake target:

- Source: `src/anicet.cpp`
- CMakeLists: `src/CMakeLists.txt`
- Output: `build/src/anicet`


# 2. Installation Notes

## 2.1. Install to Staging Directory

From build directory
```
$ cmake --install . --prefix ../install
```

Or specify custom prefix during configuration
```
$ cmake -DCMAKE_INSTALL_PREFIX=/custom/path ..
$ cmake --build . --parallel
$ cmake --install .
```

## 2.2. Install to Android Device

Create directory on device
```bash
$ adb shell "mkdir -p /data/local/tmp/bin"
```

Push encoder binaries
```
$ adb push ../install/bin/* /data/local/tmp/bin/
```

Set permissions
```
$ adb shell "chmod +x /data/local/tmp/bin/*"
```

Verify installation
```
$ adb shell /data/local/tmp/bin/anicet --help
$ adb shell "ls -la /data/local/tmp/bin/"
```

## 2.3. Quick Install Script

Create a helper script `install-to-device.sh`:

```
#!/bin/bash
set -e

INSTALL_DIR="${1:-install}"
DEVICE_PATH="${2:-/data/local/tmp}"

echo "Installing from $INSTALL_DIR to $DEVICE_PATH on device..."

adb shell "mkdir -p $DEVICE_PATH/bin"
adb push $INSTALL_DIR/bin/* $DEVICE_PATH/bin/
adb shell "chmod +x $DEVICE_PATH/bin/*"

echo "Installation complete!"
adb shell "ls -la $DEVICE_PATH/bin/"
```

Usage:
```
$ chmod +x install-to-device.sh
$ ./install-to-device.sh install /data/local/tmp
```

# 3. Development Workflow

## 3.1. Typical Development Cycle

1. Initial build
```
$ mkdir build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
$ cmake --build . --parallel
```

2. Install to device
```
$ ../install-to-device.sh ../install
```

3. Make changes to anicet.cpp

4. Quick rebuild only anicet
```
$ cmake --build . --target anicet
```

5. Reinstall only anicet to device
```
$ adb push src/anicet /data/local/tmp/bin/
$ adb shell chmod +x /data/local/tmp/bin/anicet
```

6. Test on device
```
$ adb shell /data/local/tmp/bin/anicet --tag test=1 -- /system/bin/ls
```

## 3.2. Rebuild Specific Encoder

Rebuild x265 from scratch
```
$ cd build
$ rm -rf x265-build
$ cmake --build . --target x265-lib --parallel
```

Reinstall
```
$ cmake --install .
```

## 3.3. Reconfigure Build

To change build options, reconfigure from build directory
```
$ cd build
$ cmake -DBUILD_JPEGLI=OFF ..
$ cmake --build . --parallel
```

Or start fresh:
```
$ rm -rf build
$ mkdir build && cd build
$ cmake <new-options> ..
$ cmake --build . --parallel
```

## 3.4. Cleaning

Clean build artifacts (from build directory)
```
$ cd build
$ cmake --build . --clean
```

Or remove entire build directory
```
$ cd ..
$ rm -rf build
```

Clean installed files on device
```
$ adb shell "rm -rf /data/local/tmp/bin"
```

## 3.5. Build Output Structure

```
build/
+--- x265-build/              # x265 build artifacts
+--- svt-av1-build/           # SVT-AV1 build artifacts
+--- jpegli-build/            # jpegli build artifacts
+--- libjpeg-turbo-build/     # libjpeg-turbo build artifacts
+--- jxs-build/               # JXS build artifacts
+--- src/
      +--- anicet             # anicet wrapper binary
+--- CMakeCache.txt           # CMake configuration cache

install/
+--- bin/                     # Installed encoder binaries and anicet
+--- lib/                     # Libraries (if any)
+--- include/                 # Headers (if any)
```


# 4. Build and Performance Considerations

## 4.1. Static Linking

All encoders and anicet are built with static linking (`-static`) to avoid runtime dependency issues on Android. This increases binary size but ensures portability.

To disable static linking for anicet:
```bash
cmake -DANICET_STATIC=OFF ..
```

## 4.2. Optimization Flags

Default build uses `-O2` optimization. Build configurations:
* `Release`: `-O3 -DNDEBUG` (maximum optimization)
* `Debug`: `-g` (debugging symbols, no optimization)
* `RelWithDebInfo`: `-O2 -g -DNDEBUG` (optimized with debug info)
* `MinSizeRel`: `-Os -DNDEBUG` (optimize for size)

## 4.3. SIMD Support

* libjpeg-turbo: NEON SIMD enabled automatically for ARM64
* x265: High bit depth support enabled
* SVT-AV1: Platform-optimized code paths with runtime CPU detection



# 5. Reading

* [Android NDK Documentation](https://developer.android.com/ndk)
* [CMake Documentation](https://cmake.org/documentation/)
* [CMake Android Toolchain](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-for-android)
* Individual encoder documentation in `lib/*/README.md`



# Appendix 1: Prerequisites

## A1.1. Required Software

1. Android NDK (r21 or later)
- Download: https://developer.android.com/ndk/downloads
- Default path: `/opt/android_sdk/ndk/<version>`
- Set `ANDROID_NDK` or `ANDROID_NDK_ROOT` environment variable

2. CMake (3.18 or later)
   ```bash
   sudo apt-get install cmake  # Ubuntu/Debian
   ```

3. Build Tools
   ```bash
   sudo apt-get install build-essential
   ```

4. ADB (Android Debug Bridge)
   ```bash
   sudo apt-get install adb
   ```

5. Rooted Android Device
- USB debugging enabled
- Root access (for writing to `/data/local/tmp`)
- ARM64 architecture (aarch64)


## A1.2. Verify Prerequisites

```bash
# Check CMake version
cmake --version  # Should be 3.18+

# Check NDK
ls $ANDROID_NDK  # Should show NDK directory

# Check ADB
adb devices  # Should show your connected device

# Check device architecture
adb shell getprop ro.product.cpu.abi  # Should show arm64-v8a
```


# Appendix 2: CMake Options

## A2.1. Platform Configuration

`CMAKE_BUILD_TYPE`: Build type: `Release` (default), `Debug`, `RelWithDebInfo`, `MinSizeRel`
`ANDROID_ABI`: Target ABI (default: `arm64-v8a`)
- Common values: `arm64-v8a`, `armeabi-v7a`, `x86`, `x86_64`
`ANDROID_PLATFORM`: Android API level (default: `android-21`)
- Examples: `android-21`, `android-28`, `android-30`
`CMAKE_INSTALL_PREFIX`: Installation directory (default: system-specific)

## A2.2. Encoder Selection

Build specific encoders only:

`BUILD_ENCODERS`: Build all encoder libraries (default: `ON`)
`BUILD_X265`: Build x265 encoder (default: `ON`)
`BUILD_SVT_AV1`: Build SVT-AV1 encoder (default: `ON`)
`BUILD_JPEGLI`: Build jpegli encoder (default: `ON`)
`BUILD_LIBJPEG_TURBO`: Build libjpeg-turbo encoder (default: `ON`)
`BUILD_JXS`: Build JXS encoder (default: `ON`)

## A2.3. anicet Wrapper Options

`ANICET_STATIC`: Build anicet with static linking (default: `ON`)
`ANICET_STRIP`: Strip symbols from anicet binary (default: `ON`)


## A2.4. Custom Compiler Flags

```
$ cmake -DCMAKE_CXX_FLAGS="-O3 -march=native" ..
```

## A2.5. Cross-Compilation for Different Architectures

Build for ARMv7 (32-bit)
```
$ cmake -DANDROID_ABI=armeabi-v7a \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
```

Build for x86_64 (Android emulator)
```
$ cmake -DANDROID_ABI=x86_64 \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
```

## A2.6. Verbose Build Output

See full compiler commands
```
$ cmake --build . --parallel --verbose
```

Or set during configuration
```
$ cmake -DCMAKE_VERBOSE_MAKEFILE=ON ..
$ cmake --build . --parallel
```


# Appendix 3. Build Examples

## A3.1. Build All Encoders (Default)

```
$ mkdir build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DANDROID_ABI=arm64-v8a \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
$ cmake --build . --parallel
```

## A3.2. Build Only Specific Encoders

Example: build only x265 and SVT-AV1
```
$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_JPEGLI=OFF \
      -DBUILD_LIBJPEG_TURBO=OFF \
      -DBUILD_JXS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
$ cmake --build . --parallel
```

## A3.3. Build Only anicet Wrapper (No Encoders)

```
$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_ENCODERS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
$ cmake --build . --target anicet --parallel
```

## A3.4 Build for Different Android API Level

```
$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DANDROID_PLATFORM=android-28 \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
$ cmake --build . --parallel
```

## A3.5. Debug Build

```
$ cmake -DCMAKE_BUILD_TYPE=Debug \
      -DANICET_STRIP=OFF \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      ..
$ cmake --build . --parallel
```

## A3.6. Build for Host (Testing)

Build for your local machine (no Android toolchain).
```
$ mkdir build-host && cd build-host
$ cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_ENCODERS=OFF \
      ..
$ cmake --build . --parallel
```

Then run locally
```
$ ./src/anicet --tag test=1 -- ls -la
```


# Appendix 4: Troubleshooting

## A4.1. NDK Not Found

**Error:**
```
CMake Error: CMAKE_TOOLCHAIN_FILE not found
```

**Solution:** Set the correct NDK path:

Option 1: Export environment variable
```
$ export ANDROID_NDK=/opt/android_sdk/ndk/29.0.14033849
$ cmake ..
```

Option 2: Specify toolchain file directly
```
$ cmake -DCMAKE_TOOLCHAIN_FILE=/opt/android_sdk/ndk/29.0.14033849/build/cmake/android.toolchain.cmake ..
```

## A4.2. CMake Version Too Old

**Error:**
```
CMake Error: CMake 3.18 or higher is required
```

**Solution:** Upgrade CMake:

Ubuntu/Debian: add Kitware repository for latest CMake
```
$ sudo apt-get install software-properties-common
$ sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'
$ sudo apt-get update
$ sudo apt-get install cmake
```

Or download from cmake.org

## A4.3. Encoder Build Fails

If a specific encoder fails to build:

Build without the problematic encoder
```
$ cmake -DBUILD_X265=OFF ..
```

Or build only encoders you need
```
$ cmake -DBUILD_ENCODERS=OFF ..  # Disable all
$ cmake -DBUILD_SVT_AV1=ON ..    # Enable only SVT-AV1
```

## A4.4. ADB Connection Issues

Check device connection
```
$ adb devices
```

Restart ADB server
```
$ adb kill-server
$ adb start-server
```

Check device accessibility
```
$ adb shell "echo Connection OK"
```

## A4.5. Build Hangs

Some encoders (especially x265 and SVT-AV1) can take significant time to build:
- Expect 5-15 minutes total build time on modern hardware
- Use `--parallel` for parallel builds (default)
- Monitor progress with verbose output: `cmake --build . --parallel --verbose`


## A4.6. Out of Memory During Build

Limit parallel jobs:

Limit to 4 parallel jobs
```
$ cmake --build . --parallel 4
```

Or use single-threaded build
```
$ cmake --build .
```
