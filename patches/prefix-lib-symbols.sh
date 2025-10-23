#!/bin/bash
# Prefix all global symbols in a static library to avoid conflicts
# Usage: prefix-lib-symbols.sh <input.a> <output.a> <prefix>

set -e

INPUT_LIB="$1"
OUTPUT_LIB="$2"
PREFIX="$3"

if [ -z "$INPUT_LIB" ] || [ -z "$OUTPUT_LIB" ] || [ -z "$PREFIX" ]; then
    echo "Usage: $0 <input.a> <output.a> <prefix>"
    exit 1
fi

# Copy input to output
cp "$INPUT_LIB" "$OUTPUT_LIB"

# Create a temporary directory for extraction
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Extract all object files
cd "$TEMP_DIR"
ar x "$OUTPUT_LIB"

# Prefix symbols in each object file
for obj in *.o; do
    # Use objcopy to prefix all symbols
    # We need to use the Android NDK's objcopy
    if [ -n "$ANDROID_NDK" ]; then
        OBJCOPY="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy"
    elif [ -n "$ANDROID_NDK_PATH" ]; then
        OBJCOPY="$ANDROID_NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy"
    else
        OBJCOPY="objcopy"
    fi

    "$OBJCOPY" --prefix-symbols="${PREFIX}_" "$obj"
done

# Recreate the archive
ar rcs "$OUTPUT_LIB" *.o

echo "Created $OUTPUT_LIB with symbol prefix ${PREFIX}_"
