#!/bin/bash
# -----------------------------------------------------------------------------
# create_uf2.sh – Build a drag-and-drop UF2 package for the clock generator
#
# Assumes CMake has already produced firmware binaries in ./build via:
#   cmake -DPICO_SDK_PATH=... -DPICO_BOARD=pico_w -DPICO_NO_PICOTOOL=1 -S . -B build
#   cmake --build build
#
# The script uses uf2conv.py to turn web_clockgen.bin into web_clockgen.uf2.
# If uf2conv.py or the families database are missing they are downloaded
# from Microsoft's UF2 repository.
# -----------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
UF2_TOOL="$SCRIPT_DIR/uf2conv.py"
UF2_FAMILIES="$SCRIPT_DIR/uf2families.json"
INPUT_BIN="$BUILD_DIR/web_clockgen.bin"
OUTPUT_UF2="$BUILD_DIR/web_clockgen.uf2"
UF2_FAMILY="0xe48bff56" # Raspberry Pi RP2040 family identifier

if [ ! -f "$INPUT_BIN" ]; then
    echo "❌ Error: $INPUT_BIN not found. Build the firmware first (cmake --build build)."
    exit 1
fi

if [ ! -f "$UF2_TOOL" ]; then
    echo "Downloading uf2conv.py..."
    curl -Ls -o "$UF2_TOOL" https://raw.githubusercontent.com/microsoft/uf2/master/utils/uf2conv.py
    chmod +x "$UF2_TOOL"
fi

if [ ! -f "$UF2_FAMILIES" ]; then
    echo "Downloading uf2families.json..."
    curl -Ls -o "$UF2_FAMILIES" https://raw.githubusercontent.com/microsoft/uf2/master/utils/uf2families.json
fi

echo "Converting $INPUT_BIN to UF2..."
python3 "$UF2_TOOL" --base 0x10000000 --family "$UF2_FAMILY" --output "$OUTPUT_UF2" "$INPUT_BIN" > /dev/null

echo "✅ UF2 created at $OUTPUT_UF2"
