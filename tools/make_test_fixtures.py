#!/usr/bin/env python3
"""Writes the binary test fixtures of tests/fixtures/install: a tiny raster font
(.fon, an NE module with one FNT resource) and pointer files (.cur), and the
pictures of the fixture mods in tests/fixtures/mods (24-bit BMPs of simple
shapes on black), all drawn here for OpenSE4's tests, nothing taken from any
game. Run it again after changing it; the tests read the files it writes.

    python3 tools/make_test_fixtures.py
"""

import os
import struct

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fixtures", "install")


def fnt(face, glyphs, height, ascent, leading):
    """A version 2 FNT: glyphs maps a character to rows of '#'/'.' strings."""
    first, last = min(glyphs), max(glyphs)
    count = ord(last) - ord(first) + 1
    header = 118
    table = header + (count + 1) * 4
    bitmaps = bytearray()
    entries = []
    for i in range(count):
        ch = chr(ord(first) + i)
        rows = glyphs.get(ch, ["." * 2] * height)
        width = len(rows[0])
        entries.append((width, table + len(bitmaps)))
        for col in range((width + 7) // 8):
            for row in rows:
                byte = 0
                for bit in range(8):
                    x = col * 8 + bit
                    if x < width and row[x] == "#":
                        byte |= 0x80 >> bit
                bitmaps.append(byte)
    face_at = table + len(bitmaps)
    data = bytearray(header)
    struct.pack_into("<H", data, 0, 0x0200)
    struct.pack_into("<H", data, 66, 0)          # raster
    struct.pack_into("<H", data, 68, 8)          # points
    struct.pack_into("<H", data, 70, 96)         # vertical resolution
    struct.pack_into("<H", data, 72, 96)
    struct.pack_into("<H", data, 74, ascent)
    struct.pack_into("<H", data, 76, leading)
    struct.pack_into("<H", data, 83, 400)        # weight
    struct.pack_into("<H", data, 88, height)
    data[95] = ord(first)
    data[96] = ord(last)
    data[97] = 0
    data[98] = 0
    for width, offset in entries:
        data += struct.pack("<HH", width, offset)
    data += struct.pack("<HH", 2, 0)             # the sentinel entry
    data += bitmaps
    data += face.encode("ascii") + b"\0"
    struct.pack_into("<I", data, 105, face_at)
    struct.pack_into("<I", data, 2, len(data))
    return bytes(data)


def fon(font):
    """Wraps a FNT in a minimal MZ + NE module with an RT_FONT resource."""
    shift = 4
    ne = 0x80
    data = bytearray(ne + 0x40)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, ne)
    data[ne:ne + 2] = b"NE"
    struct.pack_into("<H", data, ne + 0x24, 0x40)
    rt = ne + 0x40
    font_at = 0x200
    table = struct.pack("<H", shift)
    table += struct.pack("<HHI", 0x8008, 1, 0)                     # RT_FONT, one resource
    table += struct.pack("<HHHHHH", font_at >> shift, (len(font) + 15) >> shift, 0x1030, 0x8001, 0, 0)
    table += struct.pack("<H", 0)                                  # end of the types
    data += table
    data += bytes(font_at - len(data))
    data += font
    data += bytes((-len(font)) % 16)
    return bytes(data)


def cur(rows, hot, bits=1, palette=((0, 0, 0), (255, 255, 255))):
    """A cursor: rows of 'B' black, 'W' white, '.' transparent, 'X' screen-inverting."""
    height, width = len(rows), len(rows[0])

    def stride(b):
        return (width * b + 31) // 32 * 4

    colours = 1 << bits
    xor = bytearray()
    mask = bytearray()
    for row in reversed(rows):                     # bottom-up
        xrow = bytearray(stride(bits))
        mrow = bytearray(stride(1))
        for x, c in enumerate(row):
            index = 1 if c in "WX" else 0
            if bits == 1:
                if index:
                    xrow[x // 8] |= 0x80 >> (x % 8)
            elif bits == 4:
                xrow[x // 2] |= index << (4 if x % 2 == 0 else 0)
            if c in ".X":
                mrow[x // 8] |= 0x80 >> (x % 8)
        xor += xrow
        mask += mrow
    info = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, bits, 0, len(xor) + len(mask), 0, 0, colours, 0)
    pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in list(palette) + [(0, 0, 0)] * (colours - len(palette)))
    image = info + pal + bytes(xor) + bytes(mask)
    directory = struct.pack("<HHH", 0, 2, 1)
    entry = struct.pack("<BBBBHHII", width % 256, height % 256, 0, 0, hot[0], hot[1], len(image), 6 + 16)
    return directory + entry + image


def write(path, data):
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(data)


GLYPHS = {
    "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["##.", "..#", ".#.", "#..", "###"],
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
}

POINTER = [
    "B.......",
    "BB......",
    "BWB.....",
    "BWWB....",
    "BWWWB...",
    "BWBB....",
    "BB.X....",
    "B.......",
]

def bmp(width, height, pixel):
    """A 24-bit bottom-up BMP; pixel(x, y) gives (r, g, b) with y = 0 at the top."""
    row = (width * 3 + 3) // 4 * 4
    data = bytearray()
    for y in range(height - 1, -1, -1):
        line = bytearray()
        for x in range(width):
            r, g, b = pixel(x, y)
            line += bytes((b, g, r))
        data += line + bytes(row - len(line))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    return header + info + bytes(data)


def carrier(size):
    """A wedge-shaped hull with a flight deck stripe, pointing up, on black."""
    def pixel(x, y):
        cx = (size - 1) / 2
        u, v = (x - cx) / size, y / size          # v: 0 at the nose, 1 at the stern
        half = 0.08 + 0.32 * v                     # the hull widens towards the stern
        if not (0.08 <= v <= 0.92 and abs(u) <= half):
            return (0, 0, 0)
        if abs(u) <= 0.04 and v > 0.25:
            return (240, 200, 60)                  # the deck stripe
        if v > 0.85:
            return (90, 160, 255)                  # the engines' glow
        shade = int(150 + 80 * (1 - abs(u) / half))
        return (shade // 2, shade, shade)          # a teal hull, brighter along the keel
    return bmp(size, size, pixel)


def cutter(size):
    """A small diamond, for the classic fixture mod's replacement picture."""
    def pixel(x, y):
        c = (size - 1) / 2
        return (220, 90, 200) if abs(x - c) + abs(y - c) <= size * 0.35 else (0, 0, 0)
    return bmp(size, size, pixel)


MODS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fixtures", "mods")


def write_mod(path, data):
    full = os.path.join(MODS, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(data)


write_mod("escort-hull/assets/Pictures/RaceGeneric/Generic_Mini_EscortCarrier.bmp", carrier(36))
write_mod("escort-hull/assets/Pictures/RaceGeneric/Generic_Portrait_EscortCarrier.bmp", carrier(128))
write_mod("classic-names/Pictures/RaceGeneric/Generic_Mini_Cutter.bmp", cutter(36))

write("Path.txt", b"*BEGIN*\r\nUsing Mod Directory   := TestMod\r\n*END*\r\n")
write("Fonts/TestFace.fon", fon(fnt("Base Face", GLYPHS, 5, 4, 1)))
write("TestMod/Fonts/TestFace.fon", fon(fnt("Mod Face", GLYPHS, 5, 4, 1)))
write("Fonts/OnlyBase.fon", fon(fnt("Only Base", GLYPHS, 5, 4, 0)))
write("Pictures/Game/Normal.cur", cur(POINTER, (2, 2)))
write("Pictures/Game/Target.cur", cur(POINTER, (3, 4), bits=4, palette=((0, 0, 0), (255, 0, 0))))
