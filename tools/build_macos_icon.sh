#!/bin/bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: build_macos_icon.sh MASTER_PNG ICONSET_DIR OUTPUT_ICNS" >&2
    exit 2
fi

master=$1
iconset=$2
output=$3
expected_hash=baabe140bf9398af64402b64300ce2b6a85d244273f328b9cb9b003e101271f5
actual_hash=$(shasum -a 256 "$master" | awk '{print $1}')
if [[ "$actual_hash" != "$expected_hash" ]]; then
    echo "approved logo master hash mismatch" >&2
    exit 1
fi

dimensions=$(sips -g pixelWidth -g pixelHeight "$master")
grep -q 'pixelWidth: 1254' <<<"$dimensions"
grep -q 'pixelHeight: 1254' <<<"$dimensions"

mkdir -p "$iconset"
crop="$iconset/DuckEggs-approved-viewport.png"
sips -c 1205 1205 --cropOffset 32 28 "$master" --out "$crop" >/dev/null

make_icon() {
    local size=$1
    local name=$2
    sips -z "$size" "$size" "$crop" --out "$iconset/$name" >/dev/null
}

make_icon 16 icon_16x16.png
make_icon 32 icon_16x16@2x.png
make_icon 32 icon_32x32.png
make_icon 64 icon_32x32@2x.png
make_icon 128 icon_128x128.png
make_icon 256 icon_128x128@2x.png
make_icon 256 icon_256x256.png
make_icon 512 icon_256x256@2x.png
make_icon 512 icon_512x512.png
make_icon 1024 icon_512x512@2x.png
rm -f "$crop"
iconutil -c icns "$iconset" -o "$output"
