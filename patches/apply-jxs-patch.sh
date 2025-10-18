#!/bin/sh
# Apply JXS strcat_s portability fix patch idempotently

MARKER="$1"
TARGET="$2"
FILE="$TARGET/programs/cmdline_options.c"

# Check if already patched
if [ -f "$MARKER" ]; then
    echo "Patch already applied (marker exists), skipping..."
    exit 0
fi

# Check if file needs patching
if grep -q "^// #ifndef _MSC_VER" "$FILE"; then
    echo "Uncommenting strcat_s portable implementation..."
    # Uncomment the portable strcat_s implementation (lines 65-75)
    sed -i '65,75 s|^// ||' "$FILE"
    echo "Patch applied successfully"
else
    echo "File already patched or format different, skipping..."
fi

# Create marker
touch "$MARKER"
