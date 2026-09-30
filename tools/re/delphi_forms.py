#!/usr/bin/env python3
"""Lists the UI forms stored in a Delphi executable as readable text.

Delphi programs keep each form (window) as a binary "TPF0" resource: the
controls, their positions, captions and event-handler names. This script reads
them from the player's own executable and writes one text file per form.

    tools/re/delphi_forms.py SE4.EXE OUTDIR

OUTDIR must be under reference/re/ (gitignored): the output describes the
original program and must never be committed (see docs/CLEANROOM.md).
"""

import os
import struct
import sys

import pefile

RT_RCDATA = 10


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def take(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def short_string(self):
        n = self.u8()
        return self.take(n).decode("latin-1")

    def i(self, fmt, n):
        return struct.unpack("<" + fmt, self.take(n))[0]


def value(r, depth):
    t = r.u8()
    if t == 0:
        return "nil"
    if t == 1:  # list
        items = []
        while r.d[r.p] != 0:
            items.append(value(r, depth))
        r.u8()
        return "(" + " ".join(items) + ")"
    if t == 2:
        return str(r.i("b", 1))
    if t == 3:
        return str(r.i("h", 2))
    if t == 4:
        return str(r.i("i", 4))
    if t == 5:
        raw = r.take(10)
        mant = int.from_bytes(raw[:8], "little")
        exp = int.from_bytes(raw[8:], "little")
        sign = -1 if exp & 0x8000 else 1
        exp &= 0x7FFF
        return repr(sign * mant * 2.0 ** (exp - 16383 - 63)) if exp else "0.0"
    if t in (6, 7):  # string, identifier
        s = r.short_string()
        return repr(s) if t == 6 else s
    if t == 8:
        return "False"
    if t == 9:
        return "True"
    if t == 10:  # binary
        n = r.i("i", 4)
        r.take(n)
        return f"{{binary {n} bytes}}"
    if t == 11:  # set
        items = []
        while True:
            s = r.short_string()
            if not s:
                break
            items.append(s)
        return "[" + ", ".join(items) + "]"
    if t == 12:  # long string
        n = r.i("i", 4)
        return repr(r.take(n).decode("latin-1"))
    if t == 13:
        return "nil"
    if t == 14:  # collection
        out = ["<"]
        while r.d[r.p] != 0:
            if r.d[r.p] in (2, 3, 4):
                out.append("  " * (depth + 1) + f"item [{value(r, depth)}]")
            else:
                out.append("  " * (depth + 1) + "item")
            props = properties(r, depth + 2)
            out += props
            out.append("  " * (depth + 1) + "end")
        r.u8()
        return "\n".join(out) + ">"
    if t == 15:
        return repr(r.i("f", 4))
    if t in (16, 17):
        return repr(r.i("d", 8))
    if t == 18:  # wide string
        n = r.i("i", 4)
        return repr(r.take(n * 2).decode("utf-16-le"))
    if t == 19:
        return str(r.i("q", 8))
    if t == 20:
        n = r.i("i", 4)
        return repr(r.take(n).decode("utf-8", "replace"))
    raise ValueError(f"unknown value type {t} at {r.p - 1}")


def properties(r, depth):
    out = []
    while True:
        name = r.short_string()
        if not name:
            return out
        out.append("  " * depth + f"{name} = {value(r, depth)}")


def obj(r, depth):
    b = r.d[r.p]
    prefix = ""
    if b & 0xF0 == 0xF0:
        flags = r.u8() & 0x0F
        if flags & 2:
            prefix = f" [{value(r, depth)}]"
        if flags & 1:
            prefix += " inherited"
    cls = r.short_string()
    name = r.short_string()
    lines = ["  " * depth + f"object {name}: {cls}{prefix}"]
    lines += properties(r, depth + 1)
    while r.d[r.p] != 0:
        lines += obj(r, depth + 1)
    r.u8()
    lines.append("  " * depth + "end")
    return lines


def forms(pe):
    for entry in getattr(pe, "DIRECTORY_ENTRY_RESOURCE", pefile.Structure).entries:
        if entry.id != RT_RCDATA:
            continue
        for res in entry.directory.entries:
            name = str(res.name) if res.name else str(res.id)
            for lang in res.directory.entries:
                data = pe.get_data(lang.data.struct.OffsetToData, lang.data.struct.Size)
                if data[:4] == b"TPF0":
                    yield name, data


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    exe, outdir = sys.argv[1], sys.argv[2]
    if "reference" not in os.path.abspath(outdir).split(os.sep):
        print("refusing to write outside reference/ (see docs/CLEANROOM.md)", file=sys.stderr)
        return 2
    os.makedirs(outdir, exist_ok=True)
    pe = pefile.PE(exe)
    count = 0
    for name, data in forms(pe):
        r = Reader(data)
        r.take(4)
        try:
            text = "\n".join(obj(r, 0))
        except (ValueError, IndexError, struct.error) as e:
            text = f"(could not read: {e})"
        with open(os.path.join(outdir, name + ".txt"), "w") as f:
            f.write(text + "\n")
        count += 1
    print(f"{count} forms written to {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
