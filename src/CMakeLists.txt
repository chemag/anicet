# CMakeLists.txt for anicet wrapper

# Define the anicet executable
add_executable(anicet anicet.cpp)

# Set C++ standard
set_target_properties(anicet PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Compiler flags
target_compile_options(anicet PRIVATE
    -O2
    -Wall
    -Wextra
    -Werror
)

# Linker flags
option(ANICET_STATIC "Build anicet with static linking" ON)
option(ANICET_STRIP "Strip symbols from anicet binary" ON)

if(ANICET_STATIC)
    if(ANDROID)
        # Android: use -static for full static linking
        target_link_options(anicet PRIVATE -static)
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

# Android MediaCodec encoder (Android only)
if(ANDROID)
    add_executable(android-mediacodec android-mediacodec.cpp)

    set_target_properties(android-mediacodec PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    target_compile_options(android-mediacodec PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )

    # Link MediaCodec NDK library
    target_link_libraries(android-mediacodec
        mediandk  # MediaCodec, MediaFormat APIs
        log       # Android logging
    )

    if(ANICET_STATIC)
        # Static link C++ stdlib, but mediandk must be dynamic
        target_link_options(android-mediacodec PRIVATE -static-libgcc -static-libstdc++)
    endif()

    if(ANICET_STRIP)
        target_link_options(android-mediacodec PRIVATE -s)
    endif()

    install(TARGETS android-mediacodec
        RUNTIME DESTINATION bin
        COMPONENT android-mediacodec
    )

    message(STATUS "  MediaCodec tool:        ON")
else()
    message(STATUS "  MediaCodec tool:        OFF (Android only)")
endif()

# Print anicet build configuration
message(STATUS "----------------------------------------------")
message(STATUS "anicet Wrapper Configuration:")
message(STATUS "  Static linking: ${ANICET_STATIC}")
message(STATUS "  Strip symbols:  ${ANICET_STRIP}")
message(STATUS "----------------------------------------------")
