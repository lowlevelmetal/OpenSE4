#!/usr/bin/env python3
"""Applies structure layouts and global names to a Ghidra project.

Run with the pyghidra virtual environment (GHIDRA_INSTALL_DIR set):

    tools/re/ghidra_types.py PROJECT_DIR PROJECT_NAME TYPES.tsv

TYPES.tsv holds tab-separated lines:
    struct  NAME  SIZE              starts a structure of SIZE bytes
    field   OFFSET  SIZE  NAME      adds a field to the last structure
    global  ADDRESS  NAME  TYPE     names a global and gives it a type, where
                                    TYPE is a structure name followed by one
                                    or more '*' (e.g. Settings* or Item**)

Typing the globals that hold the loaded data files lets the decompiler show
field names instead of offsets. TYPES.tsv and the project stay under
reference/re/ (gitignored, see docs/CLEANROOM.md).
"""

import sys

import pyghidra


def pointer_cells(program, target):
    """Addresses of 4-byte-aligned words in DATA that hold `target`."""
    import struct
    import jpype
    block = program.getMemory().getBlock("DATA")
    raw = jpype.JArray(jpype.JByte)(int(block.getSize()))
    block.getBytes(block.getStart(), raw)
    data = bytes(bytearray(b & 0xFF for b in raw))
    start = block.getStart().getOffset()
    pattern = struct.pack("<I", target)
    cells = []
    i = data.find(pattern)
    while i >= 0:
        if i % 4 == 0:
            cells.append(start + i)
        i = data.find(pattern, i + 1)
    return cells


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    project_dir, project_name, types_path = sys.argv[1:4]

    pyghidra.start()
    from ghidra.program.model.data import (
        CategoryPath, DataTypeConflictHandler, PointerDataType, StructureDataType,
        UnsignedCharDataType, UnsignedShortDataType, UnsignedIntegerDataType)
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.task import TaskMonitor

    sized = {1: UnsignedCharDataType.dataType, 2: UnsignedShortDataType.dataType,
             4: UnsignedIntegerDataType.dataType}
    category = CategoryPath("/opense4")

    project = pyghidra.open_project(project_dir, project_name)
    with pyghidra.program_context(project, "/Se4.exe") as program:
        dtm = program.getDataTypeManager()
        listing = program.getListing()
        space = program.getAddressFactory().getDefaultAddressSpace()
        structs = {}
        tx = program.startTransaction("Structure layouts")
        try:
            current = None
            globals_ = []
            for line in open(types_path):
                line = line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                cols = line.split("\t")
                if cols[0] == "struct":
                    current = StructureDataType(category, cols[1], int(cols[2]), dtm)
                    structs[cols[1]] = current
                elif cols[0] == "field":
                    off, size, name = int(cols[1], 0), int(cols[2]), cols[3]
                    if off + size > current.getLength():
                        continue
                    try:
                        current.replaceAtOffset(off, sized[size], size, name, None)
                    except Exception as e:
                        print("field %s.%s: %s" % (current.getName(), name, e))
                elif cols[0] == "global":
                    globals_.append(cols[1:4])
            for name, s in structs.items():
                structs[name] = dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER)
            for va, name, typ in globals_:
                base = typ.rstrip("*")
                dt = structs[base]
                for _ in range(len(typ) - len(base)):
                    dt = PointerDataType(dt, dtm)
                a = space.getAddress(int(va, 16))
                listing.clearCodeUnits(a, a.add(3), False)
                listing.createData(a, dt)
                program.getSymbolTable().createLabel(a, name, SourceType.USER_DEFINED)
                sym = program.getSymbolTable().getPrimarySymbol(a)
                if sym is not None and sym.getName() != name:
                    sym.setName(name, SourceType.USER_DEFINED)
                # Delphi reaches another unit's variables through a pointer
                # cell in the DATA section; type those cells as well.
                for cell in pointer_cells(program, int(va, 16)):
                    c = space.getAddress(cell)
                    listing.clearCodeUnits(c, c.add(3), False)
                    listing.createData(c, PointerDataType(dt, dtm))
                    cell_sym = program.getSymbolTable().getPrimarySymbol(c)
                    if cell_sym is not None:
                        cell_sym.setName("ref_" + name, SourceType.USER_DEFINED)
                    else:
                        program.getSymbolTable().createLabel(
                            c, "ref_" + name, SourceType.USER_DEFINED)
            print("applied %d structures and %d globals" % (len(structs), len(globals_)))
        finally:
            program.endTransaction(tx, True)
        program.save("Structure layouts", TaskMonitor.DUMMY)
    project.close()


if __name__ == "__main__":
    main()
