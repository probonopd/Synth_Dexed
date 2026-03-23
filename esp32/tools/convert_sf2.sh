#!/bin/bash
# Convert SF2 SoundFont files to SFO (OGG-compressed) format
#
# Usage: ./convert_sf2.sh [input.sf2] [output.sfo] [quality]
#
# Requires: ffmpeg (for OGG encoding), cc (for building sfotool)
#
# The SFO format is a SoundFont v2 file where the PCM sample block
# is replaced with OGG Vorbis compressed audio. TinySoundFont can
# load SFO files directly when compiled with stb_vorbis support.
#
# Quality: 0-10 (vorbis VBR, default 4 ~128kbps, lower = smaller)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SFOTOOL_DIR="$SCRIPT_DIR/sfotool"
SFOTOOL="$SFOTOOL_DIR/sfotool"

# Default soundfont (YAMAHA RX5 drum machine)
SF2_URL="https://github.com/nanakochi123456/sf2_yamaha_rx5/raw/refs/heads/master/bin/YAMAHA_RX5.sf2"
SF2_INPUT="${1:-$SCRIPT_DIR/soundfonts/YAMAHA_RX5.sf2}"
SFO_OUTPUT="${2:-$SCRIPT_DIR/../spiffs_data/drums.sfo}"
QUALITY="${3:-4}"

# Build sfotool if needed
if [ ! -x "$SFOTOOL" ]; then
    echo "Building sfotool..."
    cc -o "$SFOTOOL" "$SFOTOOL_DIR/sfotool.c" -O2
fi

# Download SF2 if needed
if [ ! -f "$SF2_INPUT" ]; then
    mkdir -p "$(dirname "$SF2_INPUT")"
    echo "Downloading YAMAHA_RX5.sf2..."
    curl -sL -o "$SF2_INPUT" "$SF2_URL"
fi

# Create temp directory
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== SF2 to SFO Conversion ==="
echo "Input:   $SF2_INPUT ($(du -h "$SF2_INPUT" | cut -f1))"
echo "Quality: $QUALITY (vorbis VBR)"

# Step 1: Extract PCM samples to WAV
echo "Step 1: Extracting PCM samples..."
"$SFOTOOL" "$SF2_INPUT" "$TMPDIR/samples.wav"

# Step 2: Compress to OGG Vorbis
echo "Step 2: Compressing to OGG Vorbis (quality $QUALITY)..."
ffmpeg -y -loglevel error -i "$TMPDIR/samples.wav" \
    -c:a libvorbis -q:a "$QUALITY" "$TMPDIR/samples.ogg"

# Step 3: Repackage as SFO
echo "Step 3: Creating SFO file..."
mkdir -p "$(dirname "$SFO_OUTPUT")"
"$SFOTOOL" "$SF2_INPUT" "$TMPDIR/samples.ogg" "$SFO_OUTPUT"

echo ""
echo "=== Done ==="
echo "SF2 size: $(du -h "$SF2_INPUT" | cut -f1)"
echo "OGG size: $(du -h "$TMPDIR/samples.ogg" | cut -f1)"
echo "SFO size: $(du -h "$SFO_OUTPUT" | cut -f1)"
echo "Ratio:    $(echo "scale=1; $(stat -c%s "$SFO_OUTPUT") * 100 / $(stat -c%s "$SF2_INPUT")" | bc)%"
echo "Output:   $SFO_OUTPUT"
