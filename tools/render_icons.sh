#!/usr/bin/env bash
# Renders the PNG sizes of the application icon from its SVG
# (packaging/linux/icons/hicolor). Needs rsvg-convert (librsvg). Run it after
# changing the SVG and commit the PNGs with it.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
hicolor="$root/packaging/linux/icons/hicolor"
id=io.github.lowlevelmetal.OpenSE4
svg="$hicolor/scalable/apps/$id.svg"

for size in 16 24 32 48 64 128 256 512; do
    dir="$hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    rsvg-convert -w "$size" -h "$size" "$svg" -o "$dir/$id.png"
    if command -v oxipng > /dev/null; then oxipng -q -o max --strip safe "$dir/$id.png"; fi
    echo "$dir/$id.png"
done
