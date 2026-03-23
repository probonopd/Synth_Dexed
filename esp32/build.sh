#!/bin/bash
# Build and flash FMRack for ESP32-S3
# Usage: ./build.sh [flash] [monitor]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# If it exists, source the ESP-IDF export script to set up the environment
# in case IDF_PATH is not already set
if [ -f "$HOME/esp/esp-idf/export.sh" ] && [ -z "$IDF_PATH" ]; then
    source "$HOME/esp/esp-idf/export.sh"
fi

# Check if IDF_PATH is set
if [ -z "$IDF_PATH" ]; then
    echo "Error: IDF_PATH not set. Run: . \$IDF_PATH/export.sh"
    exit 1
fi

# Set target if not already configured
if [ ! -f "sdkconfig" ]; then
    echo "Setting target to esp32s3..."
    idf.py set-target esp32s3
fi

# Build
echo "Building FMRack for ESP32-S3..."
idf.py build

# Generate SPIFFS image
echo "Generating SPIFFS image..."
SPIFFS_DIR="$SCRIPT_DIR/spiffs_data"
SPIFFS_IMG="$SCRIPT_DIR/build/spiffs.bin"
if [ ! -f "$SPIFFS_DIR/drums.sfo" ]; then
    echo "drums.sfo not found -- running convert_sf2.sh..."
    bash "$SCRIPT_DIR/tools/convert_sf2.sh"
fi
if [ -d "$SPIFFS_DIR" ]; then
    python3 "$IDF_PATH/components/spiffs/spiffsgen.py" \
        0x4F0000 "$SPIFFS_DIR" "$SPIFFS_IMG" \
        --page-size 256 --block-size 4096
    echo "SPIFFS image created: $SPIFFS_IMG"
fi

# Flash if requested
if [[ "$*" == *"flash"* ]]; then
    echo "Flashing..."
    idf.py flash

    # Flash SPIFFS partition
    if [ -f "$SPIFFS_IMG" ]; then
        echo "Flashing SPIFFS partition..."
        python3 -m esptool --chip esp32s3 write_flash 0x310000 "$SPIFFS_IMG"
    fi
fi

# Monitor if requested
if [[ "$*" == *"monitor"* ]]; then
    echo "Starting monitor..."
    idf.py monitor
fi

echo "Done!"
