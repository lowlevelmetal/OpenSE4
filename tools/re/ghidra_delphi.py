#!/usr/bin/env python3
"""Prepares a Ghidra project of a 32-bit Delphi executable for reading.

Run with the pyghidra virtual environment (GHIDRA_INSTALL_DIR set):

    tools/re/ghidra_delphi.py PROJECT_DIR PROJECT_NAME CLASSES.json [NAMES.tsv]

CLASSES.json is the output of tools/re/delphi_classes.py. NAMES.tsv is an
optional list of "address<TAB>name[<TAB>noreturn]" lines for functions
named by hand.

The script
  - adds Delphi's register calling convention (EAX, EDX, ECX, then the stack)
    and gives it to every function, so the decompiler finds their parameters;
  - names class records, published methods, virtual methods, message
    handlers and constructors after their classes;
  - marks Delphi string literals as strings, so they show in the decompiler.

The project must be under reference/re/ (gitignored): nothing it contains may
be committed (see docs/CLEANROOM.md).
"""

import json
import sys

import pyghidra

DELPHI_CC = "__delphi"

PROTOTYPE = """
<prototype name="__delphi" extrapop="unknown" stackshift="4">
  <input>
    <pentry minsize="1" maxsize="4"><register name="EAX"/></pentry>
    <pentry minsize="1" maxsize="4"><register name="EDX"/></pentry>
    <pentry minsize="1" maxsize="4"><register name="ECX"/></pentry>
    <pentry minsize="1" maxsize="500" align="4"><addr offset="4" space="stack"/></pentry>
  </input>
  <output killedbycall="true">
    <pentry minsize="4" maxsize="10" metatype="float" extension="float"><register name="ST0"/></pentry>
    <pentry minsize="1" maxsize="4"><register name="EAX"/></pentry>
    <pentry minsize="5" maxsize="8"><addr space="join" piece1="EDX" piece2="EAX"/></pentry>
  </output>
  <unaffected>
    <register name="ESP"/>
    <register name="EBP"/>
    <register name="ESI"/>
    <register name="EDI"/>
    <register name="EBX"/>
    <register name="DF"/>
  </unaffected>
  <killedbycall>
    <register name="ECX"/>
    <register name="EDX"/>
    <register name="ST0"/>
    <register name="ST1"/>
  </killedbycall>
  <likelytrash>
    <register name="EAX"/>
  </likelytrash>
</prototype>
"""


def is_identifier(name):
    return name and all(ch.isalnum() or ch == "_" for ch in name)


def code_bytes(program, name):
    """The initialized bytes of a memory block, as Python bytes."""
    import jpype
    block = program.getMemory().getBlock(name)
    raw = jpype.JArray(jpype.JByte)(int(block.getSize()))
    block.getBytes(block.getStart(), raw)
    return block.getStart().getOffset(), bytes(bytearray(b & 0xFF for b in raw))


def discover_code(program, classes, monitor):
    """Disassembles methods the auto-analysis missed.

    Delphi reaches many methods only through class records (virtual,
    published and message methods) or method pointers, so a call-following
    disassembler never sees them. Seeds are the class records' entries, plus
    any pointer in the image that lands exactly at the start of a gap that
    follows a finished function (after Delphi's alignment padding).
    """
    import struct
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet

    listing = program.getListing()
    space = program.getAddressFactory().getDefaultAddressSpace()
    code_start, code = code_bytes(program, "CODE")
    code_end = code_start + len(code)
    images = [code_bytes(program, b.getName()) for b in program.getMemory().getBlocks()
              if b.isInitialized() and b.getName() in ("CODE", "DATA")]

    # Keep the class records themselves out of the disassembler's way.
    for c in classes:
        start = space.getAddress(c["vmt"] - 76)
        end = space.getAddress(c["vmt"] + 4 * len(c["virtuals"]) - 1)
        if listing.getInstructions(AddressSet(start, end), True).hasNext():
            continue
        try:
            listing.clearCodeUnits(start, end, False)
            from ghidra.program.model.data import DWordDataType, ArrayDataType
            n = (c["vmt"] + 4 * len(c["virtuals"]) - (c["vmt"] - 76)) // 4
            listing.createData(start, ArrayDataType(DWordDataType.dataType, n, 4))
        except Exception:
            pass

    seeds = set()
    for c in classes:
        seeds.update(c["virtuals"])
        seeds.update(m["addr"] for m in c["published"])
        seeds.update(d["addr"] for d in c["dynamic"])
        seeds.update(v for v in c["tobject_virtuals"].values() if v)

    def undefined(va):
        return (code_start <= va < code_end and
                listing.getCodeUnitContaining(space.getAddress(va)) is not None and
                listing.getInstructionContaining(space.getAddress(va)) is None and
                listing.getDefinedDataContaining(space.getAddress(va)) is None)

    def start_code(va):
        a = space.getAddress(va)
        DisassembleCommand(a, None, True).applyTo(program, monitor)
        if listing.getInstructionAt(a) is None:
            return False
        if program.getFunctionManager().getFunctionAt(a) is None:
            CreateFunctionCmd(a).applyTo(program, monitor)
        return True

    total = 0
    for round_no in range(20):
        new = 0
        for va in sorted(seeds):
            if undefined(va) and start_code(va):
                new += 1
        seeds = set()
        # Gap starts: after a returning or jumping instruction, skip Delphi's
        # padding (mov eax,eax / lea eax,[eax+0] / nop) to a 4-byte boundary.
        gap_starts = set()
        ranges = listing.getUndefinedRanges(
            AddressSet(space.getAddress(code_start), space.getAddress(code_end - 1)),
            True, monitor)
        for r in ranges:
            s = r.getMinAddress().getOffset()
            prev = listing.getInstructionBefore(r.getMinAddress())
            if prev is None or not prev.getFlowType().isTerminal() and \
                    not prev.getFlowType().isUnConditional():
                continue
            p = s
            while p % 4:
                off = p - code_start
                if code[off:off + 2] == b"\x8b\xc0":
                    p += 2
                elif code[off:off + 3] == b"\x8d\x40\x00":
                    p += 3
                elif code[off] == 0x90:
                    p += 1
                else:
                    break
            if p % 4 == 0 and p <= r.getMaxAddress().getOffset():
                gap_starts.add(p)
        for target in gap_starts:
            pattern = struct.pack("<I", target)
            for base, image in images:
                if image.find(pattern) >= 0:
                    seeds.add(target)
                    break
        total += new
        print("discovery round %d: %d new, %d pointed-to gap starts" % (
            round_no, new, len(seeds)))
        if not seeds or (round_no > 0 and new == 0):
            break
    return total


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    project_dir, project_name, classes_path = sys.argv[1:4]
    names_path = sys.argv[4] if len(sys.argv) == 5 else None
    classes = [c for c in json.load(open(classes_path)) if is_identifier(c["name"])]

    pyghidra.start()
    from ghidra.program.database import SpecExtension
    from ghidra.program.model.symbol import SourceType
    from ghidra.program.model.data import TerminatedStringDataType
    from ghidra.program.model.listing import CodeUnit
    from ghidra.util.task import TaskMonitor

    project = pyghidra.open_project(project_dir, project_name)
    monitor = TaskMonitor.DUMMY
    with pyghidra.program_context(project, "/Se4.exe") as program:
        af = program.getAddressFactory()
        listing = program.getListing()
        symbols = program.getSymbolTable()
        fm = program.getFunctionManager()
        memory = program.getMemory()

        def addr(va):
            return af.getDefaultAddressSpace().getAddress(va)

        def function_at(va, create=True):
            f = fm.getFunctionAt(addr(va))
            if f is None and create:
                from ghidra.app.cmd.function import CreateFunctionCmd
                cmd = CreateFunctionCmd(addr(va))
                cmd.applyTo(program, monitor)
                f = fm.getFunctionAt(addr(va))
            return f

        namespaces = {}

        def namespace(cls):
            if cls not in namespaces:
                namespaces[cls] = symbols.getOrCreateNameSpace(
                    program.getGlobalNamespace(), cls, SourceType.ANALYSIS)
            return namespaces[cls]

        def is_default(f):
            return f.getSymbol().getSource() == SourceType.DEFAULT

        def name_function(va, cls, method, comment=None, force=False):
            f = function_at(va)
            if f is None:
                return False
            if not force and not is_default(f):
                return False
            try:
                f.setParentNamespace(namespace(cls))
                f.setName(method, SourceType.ANALYSIS)
            except Exception:
                return False
            if comment:
                old = f.getComment() or ""
                f.setComment((old + "\n" + comment).strip())
            return True

        tx = program.startTransaction("Delphi calling convention")
        try:
            SpecExtension(program).addReplaceCompilerSpecExtension(PROTOTYPE, monitor)
        finally:
            program.endTransaction(tx, True)

        tx = program.startTransaction("Delphi code discovery")
        try:
            found = discover_code(program, classes, monitor)
        finally:
            program.endTransaction(tx, True)
        print("disassembled %d new code starts" % found)
        if found:
            pyghidra.analyze(program)

        tx = program.startTransaction("Delphi names")
        try:
            by_vmt = {c["vmt"]: c for c in classes}
            # Class records.
            for c in classes:
                symbols.createLabel(addr(c["vmt"]), "VMT_" + c["name"], SourceType.ANALYSIS)
            # Published methods (event handlers) and message handlers.
            named = 0
            for c in classes:
                for m in c["published"]:
                    if is_identifier(m["name"]):
                        named += name_function(m["addr"], c["name"], m["name"], force=True)
                for d in c["dynamic"]:
                    named += name_function(d["addr"], c["name"], "msg_%04x" % d["id"])
                for method, target in c["tobject_virtuals"].items():
                    parent = by_vmt.get(c["parent"])
                    if target and (not parent or parent["tobject_virtuals"].get(method) != target):
                        named += name_function(target, c["name"], method)
            # Virtual methods: the class that introduces or overrides a slot.
            for c in classes:
                for slot, target in enumerate(c["virtuals"]):
                    if not c["own"][slot]:
                        continue
                    intro = c["introduced_by"][slot]
                    comment = "virtual slot %d (offset 0x%x), introduced by %s" % (
                        slot, slot * 4, intro)
                    named += name_function(target, c["name"], "v%03d" % slot, comment)
            print("named %d class methods" % named)

            # The code section's bytes, for pattern searches.
            import struct
            import jpype
            code = memory.getBlock("CODE")
            start = code.getStart().getOffset()
            jdata = jpype.JArray(jpype.JByte)(int(code.getSize()))
            code.getBytes(code.getStart(), jdata)
            data = bytes(bytearray((b & 0xFF for b in jdata)))

            # Constructors: "MOV DL,1; MOV EAX,[class]; CALL ctor", in either
            # order of the first two, where [class] is a class's self pointer.
            self_ptrs = {c["vmt"] - 76: c["name"] for c in classes}
            ctor_users = {}
            for pattern in (b"\xb2\x01\xa1", b"\xa1"):
                i = data.find(pattern)
                while i >= 0:
                    p = i + len(pattern)
                    ref = struct.unpack_from("<I", data, p)[0] if p + 4 <= len(data) else 0
                    p += 4
                    if ref in self_ptrs:
                        if pattern == b"\xa1":
                            if data[p:p + 2] != b"\xb2\x01":
                                i = data.find(pattern, i + 1)
                                continue
                            p += 2
                        if data[p] == 0xE8:
                            rel = struct.unpack_from("<i", data, p + 1)[0]
                            target = start + p + 5 + rel
                            ctor_users.setdefault(target, set()).add(self_ptrs[ref])
                    i = data.find(pattern, i + 1)
            ctors = 0
            for target, users in ctor_users.items():
                users = sorted(users)
                if len(users) == 1:
                    ctors += name_function(target, users[0], "Create")
                else:
                    f = function_at(target, create=False)
                    if f is not None:
                        f.setComment(((f.getComment() or "") +
                                      "\nconstructor used for: " + ", ".join(users)).strip())
            print("named %d constructors" % ctors)

            # Delphi long string literals: refcount -1, length, text, NUL.
            strings = 0
            sdt = TerminatedStringDataType.dataType
            for off in range(0, len(data) - 12, 4):
                if data[off:off + 4] != b"\xff\xff\xff\xff":
                    continue
                n = struct.unpack_from("<I", data, off + 4)[0]
                if not 1 <= n <= 4096 or off + 8 + n >= len(data):
                    continue
                text = data[off + 8:off + 8 + n]
                if data[off + 8 + n] != 0:
                    continue
                if any(b < 0x20 and b not in (9, 10, 13) for b in text):
                    continue
                at = addr(start + off + 8)
                end = addr(start + off + 8 + n)
                if listing.getInstructions(at, True).hasNext():
                    first = listing.getInstructionContaining(at)
                    if first is not None:
                        continue
                    inst = listing.getInstructionAfter(at)
                    if inst is not None and inst.getAddress().compareTo(end) <= 0:
                        continue
                try:
                    listing.clearCodeUnits(at, end, False)
                    listing.createData(at, sdt, n + 1)
                    strings += 1
                except Exception:
                    pass
            print("marked %d string literals" % strings)

            # Hand-named functions.
            if names_path:
                hand = 0
                for line in open(names_path):
                    line = line.split("#", 1)[0].strip()
                    if not line:
                        continue
                    cols = line.split("\t")
                    va, name = cols[:2]
                    f = function_at(int(va, 16))
                    if f is not None:
                        if "::" in name:
                            ns, method = name.rsplit("::", 1)
                            f.setParentNamespace(namespace(ns))
                            f.setName(method, SourceType.USER_DEFINED)
                        else:
                            f.setName(name, SourceType.USER_DEFINED)
                        if "noreturn" in cols[2:]:
                            f.setNoReturn(True)
                        hand += 1
                print("applied %d hand names" % hand)

            # Every function uses the register calling convention, except
            # imports and their jump stubs.
            cc = 0
            for f in fm.getFunctions(True):
                if f.isExternal() or f.isThunk():
                    continue
                try:
                    f.setCallingConvention(DELPHI_CC)
                    cc += 1
                except Exception as e:
                    print("cc failed at", f.getEntryPoint(), e)
            print("set the register convention on %d functions" % cc)
        finally:
            program.endTransaction(tx, True)
        program.save("Delphi names", monitor)
    project.close()


if __name__ == "__main__":
    main()
