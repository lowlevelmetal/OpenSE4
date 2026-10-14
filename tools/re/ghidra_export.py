#!/usr/bin/env python3
"""Exports a Ghidra project's decompiled functions as searchable text.

Run with the pyghidra virtual environment (GHIDRA_INSTALL_DIR set):

    tools/re/ghidra_export.py PROJECT_DIR PROJECT_NAME OUT_DIR [THREADS]

Before exporting, it makes Ghidra see Delphi's exception frames (see
whole_delphi_routines) and saves that into the project, so every routine is
decompiled whole. Code that the decompiler still cannot reach (the bodies of
try/except handlers) is appended to its routine as a disassembly listing, and
its calls and strings count for that routine in the index.

Writes into OUT_DIR:
  all.c          every function, in address order, each under a header line
                 "//==== <address> <name> size=<bytes> callers=<n>"
  f/<address>.c  the same, one file per function
  index.tsv      address, name, size, callers, callees, strings used
  strings.tsv    each string literal and the functions that use it
  vtables.txt    each class's virtual methods by slot and offset

OUT_DIR must be under reference/re/ (gitignored): the output is derived from
the original program and must never be committed (see docs/CLEANROOM.md).
"""

import bisect
import json
import os
import struct
import sys
import threading

import pyghidra


def code_bytes(program, name):
    """The initialized bytes of a memory block: (start offset, bytes)."""
    import jpype
    block = program.getMemory().getBlock(name)
    raw = jpype.JArray(jpype.JByte)(int(block.getSize()))
    block.getBytes(block.getStart(), raw)
    return block.getStart().getOffset(), bytes(bytearray(b & 0xFF for b in raw))


def jump_at(code, off):
    """(target offset, length) of a jmp rel8/rel32 at off, or (None, 0)."""
    if off + 5 <= len(code) and code[off] == 0xE9:
        return off + 5 + struct.unpack_from("<i", code, off + 1)[0], 5
    if off + 2 <= len(code) and code[off] == 0xEB:
        return off + 2 + struct.unpack_from("<b", code, off + 1)[0], 2
    return None, 0


def whole_delphi_routines(program, monitor):
    """Makes Ghidra follow Delphi's exception frames and find every routine.

    Delphi ends the protected part of a try/finally with "push CONTINUE",
    then the finally code, then a bare "ret" that pops CONTINUE and so jumps
    to the code after the finally; the handler stub "jmp <finally handler>;
    jmp <finally code>" follows the ret. Ghidra takes that ret for the end of
    the routine, so the function body, the decompiled code, the callers and
    the strings stopped at the first finally (about 850 routines lost their
    tail, and routines that end in a finally lost their result), and the
    routines called or pointed to only from such tails were never made
    functions at all. Here each such ret becomes a jump to CONTINUE (flow
    override, a jump reference and a one-way jump table for the decompiler),
    function bodies are recomputed, and code that is called, or pointed to
    at a 4-byte boundary where no other code runs into it, becomes a function
    (string literals and zero words disassembled by mistake in front of such
    code are cleared first).

    The except part of a try/except is entered only by the exception
    dispatcher, never by a jump, so it is disassembled here but stays outside
    the function body; the export lists it after its routine.

    Returns (finally exits followed, functions grown, functions created,
    except handler starts, addresses of handler stubs).
    """
    import java.util
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.listing import CodeUnit, FlowOverride
    from ghidra.program.model.pcode import JumpTable
    from ghidra.program.model.symbol import RefType, SourceType

    fm = program.getFunctionManager()
    listing = program.getListing()
    rm = program.getReferenceManager()
    space = program.getAddressFactory().getDefaultAddressSpace()
    base, code = code_bytes(program, "CODE")

    def addr(off):
        return space.getAddress(base + off)

    # try/finally exits: "ret; jmp <handler>; jmp FINALLY", where FINALLY
    # (the start of the finally code, at most the ret itself) follows
    # "push CONTINUE".
    exits = []
    stubs = set()
    dispatchers = set()
    off = code.find(b"\xc3")
    while off >= 0:
        target, n1 = jump_at(code, off + 1)
        fin, n2 = jump_at(code, off + 1 + n1) if n1 else (None, 0)
        if fin is not None and 5 <= fin <= off and code[fin - 5] == 0x68:
            cont = struct.unpack_from("<I", code, fin - 4)[0] - base
            if off < cont < len(code):
                exits.append((off, cont))
                stubs.update((base + off + 1, base + off + 1 + n1))
                dispatchers.add(target)
        off = code.find(b"\xc3", off + 1)
    finally_stubs = {off + 1 for off, _ in exits}
    conts = {base + cont for _, cont in exits}

    # try/except handlers: the frame set-up "push ebp; push HANDLER;
    # push fs:[reg]; mov fs:[reg],esp" names HANDLER = "jmp <dispatcher>"
    # followed by the except code, or by a table "count, (class, code)..."
    # for "on E: Class do".
    handlers = []
    off = code.find(b"\x55\x68")
    while off >= 0:
        if code[off + 6:off + 8] == b"\x64\xff" and code[off + 9:off + 11] == b"\x64\x89":
            ho = struct.unpack_from("<I", code, off + 2)[0] - base
            if ho not in finally_stubs and 0 <= ho < len(code) - 8:
                target, n = jump_at(code, ho)
                if n and target not in dispatchers:
                    stubs.add(base + ho)
                    count = struct.unpack_from("<I", code, ho + n)[0]
                    table = [struct.unpack_from("<I", code, ho + n + 8 * i + 8)[0] - base
                             for i in range(count)] if 0 < count <= 32 else []
                    if table and all(0 <= e < len(code) for e in table):
                        handlers.extend(table)
                    else:
                        handlers.append(ho + n)
        off = code.find(b"\x55\x68", off + 1)
    handler_set = {base + h for h in handlers}

    def disassemble(off):
        if listing.getInstructionAt(addr(off)) is None:
            DisassembleCommand(addr(off), None, True).applyTo(program, monitor)

    def grow(offsets):
        """Recomputes the bodies of the functions holding these addresses."""
        done = set()
        for _ in range(8):
            todo = {}
            for o in offsets:
                f = fm.getFunctionContaining(addr(o))
                if f is not None and f.getEntryPoint().getOffset() not in done:
                    todo[f.getEntryPoint().getOffset()] = f
            if not todo:
                break
            for ep, f in todo.items():
                CreateFunctionCmd.fixupFunctionBody(program, f, monitor)
                done.add(ep)
        return done

    padding = (b"\x8b\xc0", b"\x8d\x40\x00", b"\x90")

    def runs_into(a):
        """Whether the code before a (past alignment padding) falls into it."""
        prev = listing.getInstructionBefore(a)
        while prev is not None and a.getOffset() - prev.getMinAddress().getOffset() <= 3 and \
                bytes(bytearray(b & 0xFF for b in prev.getBytes())) in padding:
            prev = listing.getInstructionBefore(prev.getMinAddress())
        return prev is not None and prev.getFallThrough() is not None and \
            prev.getMaxAddress().getOffset() >= a.getOffset() - 4

    symbols = program.getSymbolTable()

    def data_end(a):
        """Where code may start after a string literal (past its -1 and
        length header) or after zero words that analysis disassembled by
        mistake at a; None when a does not look like either."""
        o = a.getOffset() - base
        if o >= 8 and code[o - 8:o - 4] == b"\xff\xff\xff\xff":
            n = struct.unpack_from("<I", code, o - 4)[0]
            if 0 < n < 0x10000 and o + n < len(code) and code[o + n] == 0:
                return (o + n + 4) & ~3
        if code[o:o + 2] == b"\x00\x00":
            e = o & ~3
            while e < len(code) - 4 and code[e:e + 4] == b"\x00\x00\x00\x00":
                e += 4
            return e if e > o else None
        return None

    def redisassemble(data, start):
        """Clears the mistaken instructions from data on and disassembles
        again from start, until the new instructions meet the old ones."""
        clear_from, at = data, start
        for _ in range(16):
            # Never into a function: its code is right.
            last = at.add(15)
            body = AddressSet(clear_from, last)
            for f in fm.getFunctionsOverlapping(body):
                for r in f.getBody().intersect(body).getAddressRanges():
                    if r.getMinAddress().compareTo(last) <= 0:
                        last = r.getMinAddress().subtract(1)
            if last.compareTo(clear_from) >= 0:
                listing.clearCodeUnits(clear_from, last, False)
            if fm.getFunctionContaining(at) is not None:
                return
            DisassembleCommand(at, None, True).applyTo(program, monitor)
            # Follow the new code; where it stops short of an instruction
            # boundary, an old instruction overlaps it: clear further.
            inst = listing.getInstructionAt(start)
            stop = None
            while inst is not None and inst.getMinAddress().getOffset() < at.getOffset() + 256:
                nxt = inst.getFallThrough()
                if nxt is None:
                    break
                follow = listing.getInstructionAt(nxt)
                if follow is None:
                    stop = nxt
                    break
                inst = follow
            if stop is None:
                return
            clear_from, at = stop, stop

    def routine_starts():
        """Code in no function that is called, or pointed to at a 4-byte
        boundary, and that no other code runs into. A string literal or zero
        words disassembled by mistake in front of a routine are cleared, and
        the routine after them is taken instead."""
        found = []
        block = program.getMemory().getBlock("CODE")
        for inst in listing.getInstructions(AddressSet(block.getStart(), block.getEnd()), True):
            a = inst.getMinAddress()
            va = a.getOffset()
            if va in stubs or va in conts or va in handler_set or \
                    fm.getFunctionContaining(a) is not None or runs_into(a):
                continue
            sym = symbols.getPrimarySymbol(a)
            if sym is not None and sym.getName().startswith("VMT_"):
                continue
            kinds = [r.getReferenceType() for r in rm.getReferencesTo(a)]
            if not (any(k.isCall() for k in kinds) or
                    (va % 4 == 0 and any(k.isData() for k in kinds))):
                continue
            end = data_end(a)
            if end is None:
                found.append(a)
                continue
            if fm.getFunctionContaining(addr(end)) is not None:
                continue
            # Code follows the data only when nothing jumps into the data.
            if end <= va - base or end - (va - base) > 0x10000:
                continue
            if end - (va - base) > 1 and rm.getReferenceDestinationIterator(
                    AddressSet(a.add(1), addr(end - 1)), True).hasNext():
                continue
            found.append((a, addr(end)))
        return found

    tx = program.startTransaction("Delphi exception frames")
    try:
        for _, cont in exits:
            disassemble(cont)
        for h in sorted(set(handlers)):
            disassemble(h)
        for r, cont in exits:
            ret = listing.getInstructionAt(addr(r))
            if ret is None or listing.getInstructionAt(addr(cont)) is None:
                continue
            if ret.getFlowOverride() != FlowOverride.BRANCH:
                ret.setFlowOverride(FlowOverride.BRANCH)
            if not any(x.getToAddress() == addr(cont) for x in ret.getReferencesFrom()):
                rm.addMemoryReference(addr(r), addr(cont), RefType.COMPUTED_JUMP,
                                      SourceType.ANALYSIS, CodeUnit.MNEMONIC)
            # A function that analysis started at CONTINUE (it is pushed, so it
            # looks like a pointer to code) is part of the routine instead.
            if fm.getFunctionAt(addr(cont)) is not None and not any(
                    x.getReferenceType().isCall() for x in rm.getReferencesTo(addr(cont))):
                fm.removeFunction(addr(cont))

        grown = grow([r for r, _ in exits])
        created = 0
        for _ in range(10):
            starts = routine_starts()
            for a in starts:
                if isinstance(a, tuple):
                    data, a = a
                    redisassemble(data, a)
                    if listing.getInstructionAt(a) is None:
                        continue
                if fm.getFunctionContaining(a) is None:
                    CreateFunctionCmd(a).applyTo(program, monitor)
                    created += 1
            if not starts:
                break
        grown |= grow([r for r, _ in exits])

        fixed = 0
        for r, cont in exits:
            f = fm.getFunctionContaining(addr(r))
            if f is None or not f.getBody().contains(addr(cont)):
                continue
            dests = java.util.ArrayList()
            dests.add(addr(cont))
            JumpTable(addr(r), dests, True, 0).writeOverride(f)
            fixed += 1
    finally:
        program.endTransaction(tx, True)
    return fixed, len(grown), created, handler_set, stubs


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    project_dir, project_name, out_dir = sys.argv[1:4]
    threads = int(sys.argv[4]) if len(sys.argv) == 5 else os.cpu_count() or 4
    os.makedirs(os.path.join(out_dir, "f"), exist_ok=True)

    pyghidra.start()
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.data import StringDataInstance
    from ghidra.program.model.listing import CodeUnitFormat
    from ghidra.util.task import TaskMonitor

    project = pyghidra.open_project(project_dir, project_name)
    with pyghidra.program_context(project, "/Se4.exe") as program:
        fixed, grown, created, handlers, stubs = whole_delphi_routines(
            program, TaskMonitor.DUMMY)
        program.save("Delphi exception frames", TaskMonitor.DUMMY)
        print("try/finally exits followed: %d, functions grown: %d, created: %d, "
              "except handlers: %d" % (fixed, grown, created, len(handlers)))

        fm = program.getFunctionManager()
        rm = program.getReferenceManager()
        listing = program.getListing()
        space = program.getAddressFactory().getDefaultAddressSpace()

        functions = [f for f in fm.getFunctions(True) if not f.isExternal()]
        print("%d functions" % len(functions))
        entries = [f.getEntryPoint().getOffset() for f in functions]

        # Instructions between a function's entry and the next one that are in
        # no function body (except handlers, unreached code): listed after the
        # routine, and their references count for it.
        extra = {}
        last = max(f.getBody().getMaxAddress().getOffset() for f in functions)
        for i, f in enumerate(functions):
            end = entries[i + 1] - 1 if i + 1 < len(functions) else last
            lines = []
            if end > entries[i]:
                gap = AddressSet(space.getAddress(entries[i]), space.getAddress(end))
                for inst in listing.getInstructions(gap, True):
                    a = inst.getMinAddress()
                    if a.getOffset() in stubs or fm.getFunctionContaining(a) is not None:
                        continue
                    lines.append(inst)
            extra[entries[i]] = lines

        def owner(a):
            f = fm.getFunctionContaining(a)
            if f is not None:
                return f.getEntryPoint().getOffset()
            i = bisect.bisect_right(entries, a.getOffset()) - 1
            return entries[i] if i >= 0 else None

        info = {}
        string_users = {}
        for f in functions:
            ep = f.getEntryPoint()
            callers = set()
            it = rm.getReferencesTo(ep)
            while it.hasNext():
                r = it.next()
                if r.getReferenceType().isCall():
                    caller = owner(r.getFromAddress())
                    if caller is not None:
                        callers.add(caller)
            callees = {c.getName(True) for c in f.getCalledFunctions(TaskMonitor.DUMMY)}
            for inst in extra[ep.getOffset()]:
                for r in inst.getReferencesFrom():
                    if r.getReferenceType().isCall():
                        c = fm.getFunctionAt(r.getToAddress())
                        if c is not None:
                            callees.add(c.getName(True))
            callees = sorted(callees)
            strings = []
            insts = list(listing.getInstructions(f.getBody(), True)) + extra[ep.getOffset()]
            for inst in insts:
                for r in inst.getReferencesFrom():
                    data = listing.getDataAt(r.getToAddress())
                    if data is not None and data.hasStringValue():
                        s = StringDataInstance.getStringDataInstance(data).getStringValue()
                        if s is not None and s not in strings:
                            strings.append(s)
                            string_users.setdefault(s, []).append(ep.getOffset())
            info[ep.getOffset()] = {
                "name": f.getName(True),
                "size": int(f.getBody().getNumAddresses()),
                "callers": sorted(callers),
                "callees": callees,
                "strings": strings,
            }

        results = {}
        queue = list(functions)
        lock = threading.Lock()

        def worker():
            di = DecompInterface()
            opts = DecompileOptions()
            opts.grabFromProgram(program)
            di.setOptions(opts)
            di.openProgram(program)
            while True:
                with lock:
                    if not queue:
                        break
                    f = queue.pop()
                r = di.decompileFunction(f, 180, TaskMonitor.DUMMY)
                if r is not None and r.decompileCompleted():
                    text = str(r.getDecompiledFunction().getC())
                else:
                    msg = r.getErrorMessage() if r is not None else "no result"
                    text = "/* decompilation failed: %s */\n" % msg
                with lock:
                    results[f.getEntryPoint().getOffset()] = text
                    if len(results) % 500 == 0:
                        print("%d/%d" % (len(results), len(functions)), flush=True)
            di.dispose()

        workers = [threading.Thread(target=worker) for _ in range(threads)]
        for w in workers:
            w.start()
        for w in workers:
            w.join()

        fmt = CodeUnitFormat.DEFAULT
        with open(os.path.join(out_dir, "all.c"), "w") as all_c:
            for ep in sorted(results):
                i = info[ep]
                header = "//==== %08x %s size=%d callers=%d\n" % (
                    ep, i["name"], i["size"], len(i["callers"]))
                body = header + results[ep] + "\n"
                if extra[ep]:
                    body += ("/* Not reached by the decompiler (except handlers and other code "
                             "entered only through the exception frame); disassembly:\n")
                    for inst in extra[ep]:
                        body += "   %08x  %s\n" % (inst.getMinAddress().getOffset(),
                                                  fmt.getRepresentationString(inst))
                    body += "*/\n\n"
                all_c.write(body)
                with open(os.path.join(out_dir, "f", "%08x.c" % ep), "w") as f:
                    f.write(body)

        with open(os.path.join(out_dir, "index.tsv"), "w") as idx:
            idx.write("address\tname\tsize\tcallers\tcallees\tstrings\n")
            for ep in sorted(info):
                i = info[ep]
                idx.write("%08x\t%s\t%d\t%s\t%s\t%s\n" % (
                    ep, i["name"], i["size"],
                    ",".join("%08x" % c for c in i["callers"]),
                    ",".join(i["callees"]),
                    " | ".join(s.replace("\t", " ").replace("\r", "\\r").replace("\n", "\\n")
                              for s in i["strings"])))

        with open(os.path.join(out_dir, "strings.tsv"), "w") as st:
            for s in sorted(string_users):
                st.write("%s\t%s\n" % (
                    s.replace("\t", " ").replace("\r", "\\r").replace("\n", "\\n"),
                    ",".join("%08x %s" % (ep, info[ep]["name"]) for ep in string_users[s])))

        classes_path = os.path.join(os.path.dirname(os.path.abspath(out_dir)), "out", "classes.json")
        if os.path.exists(classes_path):
            with open(os.path.join(out_dir, "vtables.txt"), "w") as vt:
                for c in json.load(open(classes_path)):
                    if not c["virtuals"] or not c["name"].isidentifier():
                        continue
                    vt.write("%s (parent %s, instance size %d)\n" % (
                        c["name"], c["parent_name"], c["instance_size"]))
                    for slot, target in enumerate(c["virtuals"]):
                        f = fm.getFunctionAt(program.getAddressFactory()
                                             .getDefaultAddressSpace().getAddress(target))
                        name = f.getName(True) if f is not None else "?"
                        vt.write("  slot %3d  +0x%03x  %s%s\n" % (
                            slot, slot * 4, name, "" if c["own"][slot] else "  (inherited)"))
    project.close()
    print("done")


if __name__ == "__main__":
    main()
