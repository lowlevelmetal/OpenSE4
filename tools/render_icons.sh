#!/usr/bin/env bash
# Renders the application icon's other forms from its SVG
# (packaging/linux/icons/hicolor/scalable): the PNG sizes for Linux desktops, the
# Windows icon (packaging/windows/opense4.ico) and the Windows installer's side
# picture (packaging/windows/installer-side.bmp). Needs rsvg-convert (librsvg)
# and ImageMagick. Run it after changing the SVG and commit the results with it.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
hicolor="$root/packaging/linux/icons/hicolor"
windows="$root/packaging/windows"
id=io.github.lowlevelmetal.OpenSE4
svg="$hicolor/scalable/apps/$id.svg"

for size in 16 24 32 48 64 128 256 512; do
    dir="$hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    rsvg-convert -w "$size" -h "$size" "$svg" -o "$dir/$id.png"
    if command -v oxipng > /dev/null; then oxipng -q -o max --strip safe "$dir/$id.png"; fi
    echo "$dir/$id.png"
done

mkdir -p "$windows"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Windows icon: the sizes Explorer and the taskbar use, 256 stored as PNG.
for size in 16 20 24 32 40 48 64 256; do
    rsvg-convert -w "$size" -h "$size" "$svg" -o "$tmp/$size.png"
done
magick "$tmp"/{16,20,24,32,40,48,64}.png "$tmp/small.ico"
python3 - "$tmp/small.ico" "$tmp/256.png" "$windows/opense4.ico" <<'PY'
import struct, sys
small, png = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
count = struct.unpack_from("<H", small, 4)[0]
entries = [bytearray(small[6 + 16 * i:22 + 16 * i]) for i in range(count)]
images = [small[struct.unpack_from("<I", e, 12)[0]:][:struct.unpack_from("<I", e, 8)[0]] for e in entries]
entries.append(bytearray(struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, len(png), 0)))
images.append(png)
offset = 6 + 16 * len(entries)
with open(sys.argv[3], "wb") as out:
    out.write(struct.pack("<HHH", 0, 1, len(entries)))
    for e, image in zip(entries, images):
        struct.pack_into("<I", e, 12, offset)
        out.write(e)
        offset += len(image)
    for image in images:
        out.write(image)
PY
echo "$windows/opense4.ico"

# The installer's welcome and finish pages: the planet without the icon's
# rounded square, on the same night sky, 164 x 314 as NSIS expects.
sed '/id="base/d' "$svg" > "$tmp/planet.svg"
rsvg-convert -w 168 -h 168 "$tmp/planet.svg" -o "$tmp/planet.png"
magick -size 164x314 gradient:'#25386f-#080d22' \
    "$tmp/planet.png" -geometry -2+62 -composite \
    -alpha off -type truecolor BMP3:"$windows/installer-side.bmp"
echo "$windows/installer-side.bmp"
