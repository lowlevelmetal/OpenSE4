#!/usr/bin/env python3
"""Exports a Ghidra project's decompiled functions as searchable text.

Run with the pyghidra virtual environment (GHIDRA_INSTALL_DIR set):

    tools/re/ghidra_export.py PROJECT_DIR PROJECT_NAME OUT_DIR [THREADS]

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

import json
import os
import sys
import threading

import pyghidra


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    project_dir, project_name, out_dir = sys.argv[1:4]
    threads = int(sys.argv[4]) if len(sys.argv) == 5 else os.cpu_count() or 4
    os.makedirs(os.path.join(out_dir, "f"), exist_ok=True)

    pyghidra.start()
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.program.model.data import StringDataInstance
    from ghidra.util.task import TaskMonitor

    project = pyghidra.open_project(project_dir, project_name)
    with pyghidra.program_context(project, "/Se4.exe") as program:
        fm = program.getFunctionManager()
        rm = program.getReferenceManager()
        listing = program.getListing()

        functions = [f for f in fm.getFunctions(True) if not f.isExternal()]
        print("%d functions" % len(functions))

        info = {}
        string_users = {}
        for f in functions:
            ep = f.getEntryPoint()
            callers = set()
            it = rm.getReferencesTo(ep)
            while it.hasNext():
                r = it.next()
                if r.getReferenceType().isCall():
                    caller = fm.getFunctionContaining(r.getFromAddress())
                    if caller is not None:
                        callers.add(caller.getEntryPoint().getOffset())
            callees = sorted({c.getName(True) for c in f.getCalledFunctions(TaskMonitor.DUMMY)})
            strings = []
            for inst in listing.getInstructions(f.getBody(), True):
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

        with open(os.path.join(out_dir, "all.c"), "w") as all_c:
            for ep in sorted(results):
                i = info[ep]
                header = "//==== %08x %s size=%d callers=%d\n" % (
                    ep, i["name"], i["size"], len(i["callers"]))
                body = header + results[ep] + "\n"
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
                    " | ".join(s.replace("\t", " ").replace("\n", "\\n") for s in i["strings"])))

        with open(os.path.join(out_dir, "strings.tsv"), "w") as st:
            for s in sorted(string_users):
                st.write("%s\t%s\n" % (
                    s.replace("\t", " ").replace("\n", "\\n"),
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
