#!/bin/sh
# Apply x265 pthread fix patch idempotently

MARKER="$1"
PATCH="$2"
TARGET="$3"

# Check if already patched
if [ -f "$MARKER" ]; then
    echo "Patch already applied (marker exists), skipping..."
    exit 0
fi

# Apply patch (might already be applied in source)
cd "$TARGET"
patch -p1 -N -i "$PATCH" || true

# Create marker
touch "$MARKER"
echo "Patch applied and marker created"
