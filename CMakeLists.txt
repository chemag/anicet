cmake_minimum_required(VERSION 3.18)

# Android NDK detection - MUST happen before project() call
# Note: CMAKE_TOOLCHAIN_FILE must be passed via command line for cross-compilation
# This section detects the NDK path for use in ExternalProject builds
if(DEFINED ENV{ANDROID_NDK})
    set(ANDROID_NDK_PATH $ENV{ANDROID_NDK})
    message(STATUS "Using ANDROID_NDK from environment: ${ANDROID_NDK_PATH}")
elseif(DEFINED ENV{ANDROID_NDK_ROOT})
    # If ANDROID_NDK_ROOT is set, try to find the latest NDK version
    file(GLOB NDK_VERSIONS "$ENV{ANDROID_NDK_ROOT}/*")
    list(SORT NDK_VERSIONS)
    list(REVERSE NDK_VERSIONS)
    if(NDK_VERSIONS)
        list(GET NDK_VERSIONS 0 ANDROID_NDK_PATH)
        message(STATUS "Auto-detected NDK from ANDROID_NDK_ROOT: ${ANDROID_NDK_PATH}")
    endif()
elseif(ANDROID_NDK)
    # ANDROID_NDK might be set by the toolchain file
    set(ANDROID_NDK_PATH ${ANDROID_NDK})
    message(STATUS "Using ANDROID_NDK from toolchain: ${ANDROID_NDK_PATH}")
elseif(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "android.toolchain.cmake")
    # Try to deduce NDK path from toolchain file path
    get_filename_component(ANDROID_NDK_PATH "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(ANDROID_NDK_PATH "${ANDROID_NDK_PATH}" DIRECTORY)
    get_filename_component(ANDROID_NDK_PATH "${ANDROID_NDK_PATH}" DIRECTORY)
    message(STATUS "Deduced NDK path from toolchain file: ${ANDROID_NDK_PATH}")
else()
    # Default fallback
    set(ANDROID_NDK_PATH "/opt/android_sdk/ndk/29.0.14033849")
    message(STATUS "Using default NDK path: ${ANDROID_NDK_PATH}")
endif()

project(anicet-suite VERSION 1.0.0 LANGUAGES CXX)

# Build configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android-specific settings (only when cross-compiling)
if(ANDROID)
    if(NOT ANDROID_ABI)
        set(ANDROID_ABI "arm64-v8a")
    endif()
    if(NOT ANDROID_PLATFORM)
        set(ANDROID_PLATFORM "android-21")
    endif()

    message(STATUS "Android ABI: ${ANDROID_ABI}")
    message(STATUS "Android Platform: ${ANDROID_PLATFORM}")
endif()

# Build options
option(BUILD_ENCODERS "Build encoder libraries" ON)
option(BUILD_X265 "Build x265 encoder" ON)
option(BUILD_SVT_AV1 "Build SVT-AV1 encoder" ON)
option(BUILD_JPEGLI "Build jpegli encoder" ON)
option(BUILD_LIBJPEG_TURBO "Build libjpeg-turbo encoder" ON)
option(BUILD_JXS "Build JXS encoder" ON)

# Installation directory
set(ANICET_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}" CACHE PATH "Installation directory")

# Include ExternalProject module for building encoder libraries
include(ExternalProject)

# Common CMake arguments for all encoder projects
# Use CMAKE_CACHE_ARGS for toolchain file to ensure proper propagation
set(ENCODER_CMAKE_CACHE_ARGS
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${ANICET_INSTALL_DIR}
)

set(ENCODER_CMAKE_ARGS "")

if(ANDROID)
    list(APPEND ENCODER_CMAKE_CACHE_ARGS
        -DANDROID_ABI:STRING=${ANDROID_ABI}
        -DANDROID_PLATFORM:STRING=${ANDROID_PLATFORM}
        -DANDROID_NDK:PATH=${ANDROID_NDK_PATH}
    )
endif()

# Build encoder libraries as external projects
if(BUILD_ENCODERS)
    # x265 encoder
    if(BUILD_X265)
        ExternalProject_Add(x265-lib
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/x265/source
            BINARY_DIR ${CMAKE_BINARY_DIR}/x265-build
            # Apply pthread fix patch (idempotent - only applies once)
            PATCH_COMMAND sh ${CMAKE_SOURCE_DIR}/patches/apply-x265-patch.sh
                ${CMAKE_SOURCE_DIR}/lib/x265/source/.pthread-patched
                ${CMAKE_SOURCE_DIR}/patches/x265-pthread-fix.patch
                ${CMAKE_SOURCE_DIR}/lib/x265/source
            CMAKE_CACHE_ARGS
                ${ENCODER_CMAKE_CACHE_ARGS}
            CMAKE_ARGS
                ${ENCODER_CMAKE_ARGS}
                -DENABLE_SHARED=OFF
                -DENABLE_CLI=ON
                -DHIGH_BIT_DEPTH=ON
                -DENABLE_ASSEMBLY=ON
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
            INSTALL_COMMAND ${CMAKE_COMMAND} --install .
        )
    endif()

    # SVT-AV1 encoder
    if(BUILD_SVT_AV1)
        ExternalProject_Add(svt-av1-lib
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/svt-av1
            BINARY_DIR ${CMAKE_BINARY_DIR}/svt-av1-build
            CMAKE_CACHE_ARGS
                ${ENCODER_CMAKE_CACHE_ARGS}
                # Override SVT-AV1's default output to source tree - use build tree instead
                -DCMAKE_OUTPUT_DIRECTORY:PATH=${CMAKE_BINARY_DIR}/svt-av1-build
            CMAKE_ARGS
                ${ENCODER_CMAKE_ARGS}
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_APPS=ON
                -DBUILD_DEC=OFF
                -DBUILD_TESTING=OFF
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
            INSTALL_COMMAND ${CMAKE_COMMAND} --install .
        )
    endif()

    # jpegli encoder
    if(BUILD_JPEGLI)
        ExternalProject_Add(jpegli-lib
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/jpegli
            BINARY_DIR ${CMAKE_BINARY_DIR}/jpegli-build
            CMAKE_CACHE_ARGS
                ${ENCODER_CMAKE_CACHE_ARGS}
                -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
                # Set NDK path for sjpeg's cpufeatures dependency
                -DSJPEG_ANDROID_NDK_PATH:PATH=${ANDROID_NDK_PATH}
            CMAKE_ARGS
                ${ENCODER_CMAKE_ARGS}
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_TESTING=OFF
                -DJPEGXL_ENABLE_TOOLS=ON
                -DJPEGXL_ENABLE_JPEGLI=ON
                -DJPEGXL_ENABLE_JPEGLI_LIBJPEG=ON
                # Disable OpenEXR to avoid Android dependency issues
                -DJPEGXL_ENABLE_OPENEXR=OFF
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
            INSTALL_COMMAND ${CMAKE_COMMAND} --install .
        )
    endif()

    # libjpeg-turbo encoder
    if(BUILD_LIBJPEG_TURBO)
        ExternalProject_Add(libjpeg-turbo-lib
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/libjpeg-turbo
            BINARY_DIR ${CMAKE_BINARY_DIR}/libjpeg-turbo-build
            CMAKE_CACHE_ARGS
                ${ENCODER_CMAKE_CACHE_ARGS}
            CMAKE_ARGS
                ${ENCODER_CMAKE_ARGS}
                -DENABLE_SHARED=OFF
                -DENABLE_STATIC=ON
                -DWITH_SIMD=ON
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
            INSTALL_COMMAND ${CMAKE_COMMAND} --install .
        )
    endif()

    # JXS encoder
    if(BUILD_JXS)
        ExternalProject_Add(jxs-lib
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/jxs
            BINARY_DIR ${CMAKE_BINARY_DIR}/jxs-build
            # Apply strcat_s portability fix (idempotent - only applies once)
            PATCH_COMMAND sh ${CMAKE_SOURCE_DIR}/patches/apply-jxs-patch.sh
                ${CMAKE_SOURCE_DIR}/lib/jxs/.strcat-patched
                ${CMAKE_SOURCE_DIR}/lib/jxs
            CMAKE_CACHE_ARGS
                ${ENCODER_CMAKE_CACHE_ARGS}
            CMAKE_ARGS
                ${ENCODER_CMAKE_ARGS}
                -DBUILD_SHARED_LIBS=OFF
            # Build everything including CLI tools
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
            # Install library and manually copy CLI tools (no install rules for them)
            INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/jxs-build/bin/jxs_encoder
                ${CMAKE_BINARY_DIR}/jxs-build/bin/jxs_decoder
                ${ANICET_INSTALL_DIR}/bin/
        )
    endif()
endif()

# Add anicet wrapper
add_subdirectory(src)

# Print build summary
message(STATUS "==============================================")
message(STATUS "anicet Build Configuration Summary")
message(STATUS "==============================================")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
if(ANDROID)
    message(STATUS "Target Platform: Android ${ANDROID_PLATFORM}")
    message(STATUS "Target ABI: ${ANDROID_ABI}")
else()
    message(STATUS "Target Platform: Host (${CMAKE_SYSTEM_NAME})")
endif()
message(STATUS "----------------------------------------------")
message(STATUS "Encoder Libraries:")
message(STATUS "  x265:          ${BUILD_X265}")
message(STATUS "  SVT-AV1:       ${BUILD_SVT_AV1}")
message(STATUS "  jpegli:        ${BUILD_JPEGLI}")
message(STATUS "  libjpeg-turbo: ${BUILD_LIBJPEG_TURBO}")
message(STATUS "  JXS:           ${BUILD_JXS}")
message(STATUS "==============================================")

# Custom target to build all encoders
if(BUILD_ENCODERS)
    set(ENCODER_TARGETS "")
    if(BUILD_X265)
        list(APPEND ENCODER_TARGETS x265-lib)
    endif()
    if(BUILD_SVT_AV1)
        list(APPEND ENCODER_TARGETS svt-av1-lib)
    endif()
    if(BUILD_JPEGLI)
        list(APPEND ENCODER_TARGETS jpegli-lib)
    endif()
    if(BUILD_LIBJPEG_TURBO)
        list(APPEND ENCODER_TARGETS libjpeg-turbo-lib)
    endif()
    if(BUILD_JXS)
        list(APPEND ENCODER_TARGETS jxs-lib)
    endif()

    add_custom_target(encoders
        DEPENDS ${ENCODER_TARGETS}
        COMMENT "Building all encoder libraries"
    )
endif()

# Installation helper
install(CODE "message(STATUS \"Installation complete to ${CMAKE_INSTALL_PREFIX}\")")
