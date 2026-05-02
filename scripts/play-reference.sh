#!/usr/bin/env bash
set -euo pipefail

# Convert raw float32 reference files to .wav for playback.
# Usage: bash scripts/play-reference.sh [filename]
#   No args: converts all .raw files in Tests/reference/
#   With arg: converts just that file (e.g., osc_sine_c4.raw)
#
# Requires: ffmpeg (brew install ffmpeg) or sox (brew install sox)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REF_DIR="$(dirname "$SCRIPT_DIR")/Tests/reference"
OUT_DIR="$REF_DIR/wav"
mkdir -p "$OUT_DIR"

convert_file() {
    local raw="$1"
    local name
    name="$(basename "$raw" .raw)"
    local wav="$OUT_DIR/${name}.wav"

    if command -v ffmpeg &>/dev/null; then
        ffmpeg -y -f f32le -ar 44100 -ac 1 -i "$raw" "$wav" 2>/dev/null
    elif command -v sox &>/dev/null; then
        sox -t raw -r 44100 -e floating-point -b 32 -c 1 "$raw" "$wav"
    else
        echo "Error: Install ffmpeg (brew install ffmpeg) or sox (brew install sox)"
        exit 1
    fi
    echo "  $name.wav"
}

echo "Converting reference files to WAV..."

if [ $# -gt 0 ]; then
    convert_file "$REF_DIR/$1"
else
    for raw in "$REF_DIR"/*.raw; do
        [ -f "$raw" ] || continue
        convert_file "$raw"
    done
fi

echo ""
echo "WAV files in: $OUT_DIR"
echo "Open with: open $OUT_DIR/*.wav"
