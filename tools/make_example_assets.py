#!/usr/bin/env python3
"""Makes the pictures and sounds of the example mods in mods/examples (docs/sdk/guide):
small PNGs with real transparency, drawn here from a few shapes, and a short WAV tone
made here from a formula; nothing taken from any game. Run it again after changing it;
the examples commit what it writes.

    python3 tools/make_example_assets.py           # writes the files
    python3 tools/make_example_assets.py --check   # exits 1 when a file differs

The files:

  new-hull/assets/Pictures/RaceGeneric/Generic_Mini_WrenCourier.png      36x36, the classic mini size
  new-hull/assets/Pictures/RaceGeneric/Generic_Portrait_WrenCourier.png  128x128, the classic portrait size
  weapon-line/assets/Sounds/ember.wav                                    the Ember Lance's shot: 0.18 s, 8-bit mono

Each shape is sampled 4x4 times per pixel, so its edges are soft and its alpha channel
smooth. Everything is whole-number arithmetic, so the pixels depend only on this file,
and --check (which compares pixels, not compressed bytes) holds on every computer.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path
from typing import Callable, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "mods" / "examples"

Colour = Tuple[int, int, int]
# A shape answers whether a sample point lies inside it. Points are whole numbers in
# units of 1/(100*S) of the square, S being the picture's samples across: so a shape
# given in hundredths and a sample's centre compare exactly, in integers.
Inside = Callable[[int, int, int], bool]

# ---- Shapes, in hundredths of a square with y going down ---------------------------------------


def polygon(points: Sequence[Tuple[int, int]]) -> Inside:
    """A polygon given in hundredths of the square; even-odd rule."""
    def inside(x: int, y: int, s: int) -> bool:
        pts = [(px * s, py * s) for px, py in points]
        hit = False
        j = len(pts) - 1
        for i in range(len(pts)):
            xi, yi = pts[i]
            xj, yj = pts[j]
            if (yi > y) != (yj > y):
                d = yj - yi
                lhs = (x - xi) * d
                rhs = (y - yi) * (xj - xi)
                if (lhs < rhs) if d > 0 else (lhs > rhs):
                    hit = not hit
            j = i
        return hit
    return inside


def ellipse(cx: int, cy: int, rx: int, ry: int) -> Inside:
    """An ellipse given in hundredths of the square."""
    def inside(x: int, y: int, s: int) -> bool:
        dx = x - cx * s
        dy = y - cy * s
        return dx * dx * ry * ry + dy * dy * rx * rx <= (rx * ry * s) ** 2
    return inside


# The Wren Courier: a slim hull pointing up, two cargo pods at its sides, a bridge dome
# and an engine glow at its stern. Drawn back to front; the last shape that covers a
# sample gives its colour.
WREN: List[Tuple[Inside, Colour]] = [
    (ellipse(50, 88, 13, 9), (255, 170, 60)),                                       # engine glow
    (polygon([(50, 6), (62, 30), (64, 78), (56, 88), (44, 88), (36, 78), (38, 30)]), (120, 138, 160)),  # hull
    (polygon([(22, 40), (34, 36), (36, 74), (24, 80), (18, 70)]), (92, 108, 128)),  # port pod
    (polygon([(78, 40), (66, 36), (64, 74), (76, 80), (82, 70)]), (92, 108, 128)),  # starboard pod
    (polygon([(34, 48), (66, 48), (66, 54), (34, 54)]), (150, 166, 186)),           # spar across the pods
    (polygon([(47, 34), (53, 34), (53, 70), (47, 70)]), (176, 190, 206)),           # spine
    (ellipse(50, 24, 6, 8), (90, 200, 230)),                                        # bridge dome
]


def render(shapes: List[Tuple[Inside, Colour]], size: int, samples: int = 4) -> bytes:
    """RGBA rows: each pixel's colour is the average of its samples that a shape covers,
    its alpha the share of them covered."""
    s = 2 * size * samples          # sample centres fall on odd multiples of 1/s of the square
    rows = bytearray()
    n = samples * samples
    for py in range(size):
        rows.append(0)  # PNG filter: none
        for px in range(size):
            r = g = b = covered = 0
            for sy in range(samples):
                for sx in range(samples):
                    # The sample's centre, (2k+1)/s of the square, in units of 1/(100*s).
                    x = 100 * (2 * (px * samples + sx) + 1)
                    y = 100 * (2 * (py * samples + sy) + 1)
                    colour: Optional[Colour] = None
                    for inside, c in shapes:
                        if inside(x, y, s):
                            colour = c
                    if colour is not None:
                        r += colour[0]
                        g += colour[1]
                        b += colour[2]
                        covered += 1
            if covered:
                rows += bytes((r // covered, g // covered, b // covered, covered * 255 // n))
            else:
                rows += bytes((0, 0, 0, 0))
    return bytes(rows)


def png(size: int, rgba_rows: bytes) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)   # 8-bit RGBA
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rgba_rows, 9)) + chunk(b"IEND", b"")


def pixels(data: bytes) -> Optional[Tuple[bytes, bytes]]:
    """A PNG's header and decompressed rows, as this script writes them (one IDAT)."""
    try:
        at = 8
        header = b""
        rows = b""
        while at < len(data):
            length = struct.unpack(">I", data[at:at + 4])[0]
            kind = data[at + 4:at + 8]
            body = data[at + 8:at + 8 + length]
            if kind == b"IHDR":
                header = body
            elif kind == b"IDAT":
                rows += body
            at += 12 + length
        return header, zlib.decompress(rows)
    except (struct.error, zlib.error):
        return None


# ---- Sounds ------------------------------------------------------------------------------------


def wav(rate: int, samples: bytes) -> bytes:
    """An 8-bit mono PCM WAV file."""
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    return (b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(samples)) + b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"data" + struct.pack("<I", len(samples)) + samples)


def ember_shot() -> bytes:
    """A falling buzz: a square-ish wave whose pitch drops from about 1.4 kHz to 400 Hz
    while it fades out, in whole numbers only (a phase accumulator, no sine)."""
    rate = 11025
    count = rate * 18 // 100
    out = bytearray()
    phase = 0
    for i in range(count):
        step = (1400 - 1000 * i // count) * 65536 // rate      # the pitch, as phase per sample
        phase = (phase + step) % 65536
        loud = 90 * (count - i) // count                       # fading out
        tri = phase // 128 if phase < 32768 else (65535 - phase) // 128   # 0..255, a triangle
        out.append(128 + (tri - 128) * loud // 128)
    return wav(rate, bytes(out))


SOUNDS = [
    ("weapon-line/assets/Sounds/ember.wav", ember_shot),
]


PICTURES = [
    ("new-hull/assets/Pictures/RaceGeneric/Generic_Mini_WrenCourier.png", WREN, 36),
    ("new-hull/assets/Pictures/RaceGeneric/Generic_Portrait_WrenCourier.png", WREN, 128),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 when a file differs from what this script makes")
    args = ap.parse_args()
    stale = 0
    for rel, make in SOUNDS:
        data = make()
        path = EXAMPLES / rel
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                print(f"mods/examples/{rel} differs from what tools/make_example_assets.py makes")
                stale += 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote mods/examples/{rel} ({len(data)} bytes)")
    for rel, shapes, size in PICTURES:
        data = png(size, render(shapes, size))
        path = EXAMPLES / rel
        if args.check:
            # Pixels, not bytes: another zlib may compress the same rows differently.
            if not path.exists() or pixels(path.read_bytes()) != pixels(data):
                print(f"mods/examples/{rel} differs from what tools/make_example_assets.py draws")
                stale += 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote mods/examples/{rel} ({size}x{size}, {len(data)} bytes)")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
