#!/usr/bin/env python3
"""Lists the classes compiled into a 32-bit Delphi executable.

Delphi keeps a class record (the "VMT") for every class: its name, parent,
instance size, virtual methods, published methods (form event handlers) and
message handlers. This script finds those records in the player's own
executable and writes them as JSON, so an analysis tool can name functions.

    tools/re/delphi_classes.py SE4.EXE OUT.json

OUT.json must be under reference/re/ (gitignored): the output describes the
original program and must never be committed (see docs/CLEANROOM.md).
"""

import json
import os
import struct
import sys

import pefile

# Offsets from the class pointer (Delphi 3 to 7, 32-bit).
SELF_PTR = -76
INTF_TABLE = -72
AUTO_TABLE = -68
INIT_TABLE = -64
TYPE_INFO = -60
FIELD_TABLE = -56
METHOD_TABLE = -52
DYNAMIC_TABLE = -48
CLASS_NAME = -44
INSTANCE_SIZE = -40
PARENT = -36

# Virtual methods every class inherits from TObject, at negative offsets.
TOBJECT_VIRTUALS = {
    -32: "SafeCallException",
    -28: "AfterConstruction",
    -24: "BeforeDestruction",
    -20: "Dispatch",
    -16: "DefaultHandler",
    -12: "NewInstance",
    -8: "FreeInstance",
    -4: "Destroy",
}


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in self.pe.sections:
            self.sections.append((self.base + s.VirtualAddress, s.get_data(),
                                  s.Name.rstrip(b"\0").decode("latin-1")))
        code = [s for s in self.sections if s[2] == "CODE"][0]
        self.code_start = code[0]
        self.code_end = code[0] + len(code[1])

    def find(self, va):
        for start, data, _ in self.sections:
            if start <= va < start + len(data):
                return data, va - start
        return None, None

    def u8(self, va):
        data, off = self.find(va)
        return data[off]

    def u16(self, va):
        data, off = self.find(va)
        if data is None:
            raise ValueError(f"no section at {va:#x}")
        return struct.unpack_from("<H", data, off)[0]

    def u32(self, va):
        data, off = self.find(va)
        if data is None or off + 4 > len(data):
            return None
        return struct.unpack_from("<I", data, off)[0]

    def short_string(self, va):
        data, off = self.find(va)
        if data is None:
            raise ValueError(f"no section at {va:#x}")
        n = data[off]
        return data[off + 1:off + 1 + n].decode("latin-1")

    def in_code(self, va):
        return va is not None and self.code_start <= va < self.code_end


def find_classes(img):
    data, _ = img.find(img.code_start)
    out = {}
    for off in range(0, len(data) - 80, 4):
        va = img.code_start + off
        if struct.unpack_from("<I", data, off)[0] != va + 76:
            continue
        vmt = va + 76
        name_ptr = img.u32(vmt + CLASS_NAME)
        if not img.in_code(name_ptr):
            continue
        out[vmt] = {"vmt": vmt, "name": img.short_string(name_ptr)}
    return out


def method_table(img, va):
    methods = []
    if not va:
        return methods
    count = img.u16(va)
    p = va + 2
    for _ in range(count):
        size = img.u16(p)
        addr = img.u32(p + 2)
        name = img.short_string(p + 6)
        methods.append({"addr": addr, "name": name})
        p += size
    return methods


def dynamic_table(img, va):
    out = []
    if not va:
        return out
    count = img.u16(va)
    ids = [img.u16(va + 2 + 2 * i) for i in range(count)]
    base = va + 2 + 2 * count
    for i, ident in enumerate(ids):
        out.append({"id": ident, "addr": img.u32(base + 4 * i)})
    return out


def field_table(img, va):
    fields = []
    if not va:
        return fields
    count = img.u16(va)
    p = va + 6
    for _ in range(count):
        off = img.u32(p)
        name = img.short_string(p + 6)
        fields.append({"offset": off, "name": name})
        p += 7 + len(name)
    return fields


def table(img, reader, slot):
    """Reads one of a class's tables, or nothing if it doesn't parse."""
    va = img.u32(slot)
    if not img.in_code(va):
        return []
    try:
        return reader(img, va)
    except (ValueError, TypeError, IndexError, struct.error):
        return []


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    img = Image(sys.argv[1])
    classes = find_classes(img)
    vmts = sorted(classes)

    for i, vmt in enumerate(vmts):
        c = classes[vmt]
        parent_ref = img.u32(vmt + PARENT)
        parent = img.u32(parent_ref) if parent_ref else 0
        c["parent"] = parent if parent in classes else 0
        c["instance_size"] = img.u32(vmt + INSTANCE_SIZE)
        c["published"] = table(img, method_table, vmt + METHOD_TABLE)
        c["dynamic"] = table(img, dynamic_table, vmt + DYNAMIC_TABLE)
        c["fields"] = table(img, field_table, vmt + FIELD_TABLE)
        c["tobject_virtuals"] = {
            name: img.u32(vmt + off) for off, name in TOBJECT_VIRTUALS.items()}

        # The virtual method slots run from the class pointer up to the first
        # of the class's own tables or the next class record.
        limit = vmts[i + 1] + SELF_PTR if i + 1 < len(vmts) else img.code_end
        for off in (INTF_TABLE, AUTO_TABLE, INIT_TABLE, TYPE_INFO, FIELD_TABLE,
                    METHOD_TABLE, DYNAMIC_TABLE, CLASS_NAME):
            p = img.u32(vmt + off)
            if p and vmt < p < limit:
                limit = p
        slots = []
        p = vmt
        while p < limit:
            target = img.u32(p)
            if not img.in_code(target):
                break
            slots.append(target)
            p += 4
        c["virtuals"] = slots

    def depth(vmt):
        d = 0
        while classes[vmt]["parent"]:
            vmt = classes[vmt]["parent"]
            d += 1
        return d

    for vmt in sorted(vmts, key=depth):
        c = classes[vmt]
        parent = classes.get(c["parent"])
        # A parent never has more slots than its children.
        if parent and len(c["virtuals"]) < len(parent["virtuals"]):
            c["virtuals"] = c["virtuals"] + parent["virtuals"][len(c["virtuals"]):]
        # A slot is the class's own when it introduces or overrides it.
        c["introduced_by"] = []
        c["own"] = []
        for slot, target in enumerate(c["virtuals"]):
            if parent and slot < len(parent["virtuals"]):
                c["introduced_by"].append(parent["introduced_by"][slot])
                c["own"].append(parent["virtuals"][slot] != target)
            else:
                c["introduced_by"].append(c["name"])
                c["own"].append(True)


    out = []
    for vmt in vmts:
        c = dict(classes[vmt])
        c["parent_name"] = classes[c["parent"]]["name"] if c["parent"] else ""
        out.append(c)
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
    with open(sys.argv[2], "w") as f:
        json.dump(out, f, indent=1)
    print(f"{len(out)} classes")


if __name__ == "__main__":
    main()
