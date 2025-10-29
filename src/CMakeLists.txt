# CMakeLists.txt for anicet wrapper

# Linker flags options
option(ANICET_STATIC "Build anicet with static linking" ON)
option(ANICET_STRIP "Strip symbols from anicet binary" ON)

# Android MediaCodec encoder (Android only) - define first
if(ANDROID)
    # Build MediaCodec library
    add_library(android_mediacodec_lib STATIC android_mediacodec_lib.cc anicet_common.cc)

    set_target_properties(android_mediacodec_lib PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    target_compile_options(android_mediacodec_lib PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )

    # Include directories for the library
    target_include_directories(android_mediacodec_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    )

    # Link MediaCodec NDK libraries to the library
    target_link_libraries(android_mediacodec_lib PUBLIC
        mediandk  # MediaCodec, MediaFormat APIs
        log       # Android logging
    )
endif()

# Define the anicet executable (always built)
add_executable(anicet
    anicet.cpp
    anicet_runner.cc
    anicet_parameter.cc
    anicet_runner_webp.cc
    anicet_runner_libjpegturbo.cc
    anicet_runner_jpegli.cc
    anicet_runner_x265.cc
    anicet_runner_svtav1.cc
    anicet_runner_mediacodec.cc
    anicet_common.cc
)

# Set C++ standard
set_target_properties(anicet PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Include directories for anicet
target_include_directories(anicet PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_INSTALL_PREFIX}/include
)

# Compiler flags
target_compile_options(anicet PRIVATE
    -O2
    -Wall
    -Wextra
    -Werror
)

# Link encoder libraries
# Note: x265, webp, and libjpeg-turbo use dlopen() and are not linked at compile time
target_link_libraries(anicet PRIVATE
    ${CMAKE_INSTALL_PREFIX}/lib/libSvtAv1Enc.a
    ${CMAKE_INSTALL_PREFIX}/lib/libhwy.a
    ${CMAKE_BINARY_DIR}/jpegli-build/lib/jpegli/libjpeg.so
    nlohmann_json::nlohmann_json
)

# Link android_mediacodec_lib and system libraries
if(ANDROID)
    target_link_libraries(anicet PRIVATE
        android_mediacodec_lib
        dl  # Required for dlopen/dlsym/dlclose
        c++_shared  # Explicitly use shared libc++ to avoid runtime linking issues
    )
else()
    # Add pthread and other system libraries (not needed on Android)
    target_link_libraries(anicet PRIVATE pthread m dl)
endif()

# Linker flags for anicet
if(ANICET_STATIC)
    if(ANDROID)
        # Android: use dynamic libstdc++ to avoid runtime linking issues with exceptions
        # Only statically link libgcc
        target_link_options(anicet PRIVATE -static-libgcc)
    else()
        # Host: link standard libraries statically where possible
        target_link_options(anicet PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

if(ANICET_STRIP)
    target_link_options(anicet PRIVATE -s)
endif()

# Installation
install(TARGETS anicet
    RUNTIME DESTINATION bin
    COMPONENT anicet
)

# Android MediaCodec executable (Android only)
if(ANDROID)
    # Build MediaCodec executable
    add_executable(android_mediacodec android_mediacodec.cc)

    set_target_properties(android_mediacodec PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    target_compile_options(android_mediacodec PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )

    # Include directories for the executable
    target_include_directories(android_mediacodec PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    # Link against the MediaCodec library
    target_link_libraries(android_mediacodec
        android_mediacodec_lib
        c++_shared  # Explicitly use shared libc++ to avoid runtime linking issues
    )

    if(ANICET_STATIC)
        # Use dynamic libstdc++ to avoid runtime linking issues with exceptions
        # Only statically link libgcc
        target_link_options(android_mediacodec PRIVATE -static-libgcc)
    endif()

    if(ANICET_STRIP)
        target_link_options(android_mediacodec PRIVATE -s)
    endif()

    install(TARGETS android_mediacodec
        RUNTIME DESTINATION bin
        COMPONENT android_mediacodec
    )

    message(STATUS "  MediaCodec library:     ON")
    message(STATUS "  MediaCodec tool:        ON")
else()
    message(STATUS "  MediaCodec library:     OFF (Android only)")
    message(STATUS "  MediaCodec tool:        OFF (Android only)")
endif()

# Print anicet build configuration
message(STATUS "----------------------------------------------")
message(STATUS "anicet Wrapper Configuration:")
message(STATUS "  Static linking: ${ANICET_STATIC}")
message(STATUS "  Strip symbols:  ${ANICET_STRIP}")
message(STATUS "----------------------------------------------")
