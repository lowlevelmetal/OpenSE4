"""Checks the Python examples of the SDK's documentation (docs/sdk, docs/MODDING_SDK.md and
the mods' READMEs) under CPython 3.10 or newer:

    python3 tests/sdk/python/check_doc_snippets.py [--fixtures fixtures.json] [FILE...]

A fenced block's info string says what it is:

- ```python             a complete module: it must compile and run with the opense4
                        package on the path, and every computer player it defines must
                        answer the planning calls (politics, orders, economy) on the
                        fixtures' view with testing.FakeServices, without an error;
- ```python no-run      a complete file that needs the game, a running engine or the mod's
                        other modules: it must compile;
- ```python fragment    an excerpt (a method's lines, names defined elsewhere): not checked.

The generated API reference (docs/sdk/reference) is left out: its blocks are signatures
taken from the package itself. tests/sdk/test_sdk_python.cpp runs this with the fixtures it
makes from a game of the engine fixture; without them the players are not played.
"""

import json
import os
import re
import sys
import traceback

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
KINDS = ("", "no-run", "fragment")


def markdown_files():
    out = [os.path.join(ROOT, "docs", "MODDING_SDK.md")]
    for top in (os.path.join(ROOT, "docs", "sdk"), os.path.join(ROOT, "mods")):
        for folder, dirs, files in os.walk(top):
            dirs[:] = sorted(d for d in dirs if d != "reference" and not d.startswith("."))
            out.extend(os.path.join(folder, f) for f in sorted(files) if f.endswith(".md"))
    return out


def blocks(path):
    """(line, language, words, code) of each fenced block of a Markdown file."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    found, i = [], 0
    while i < len(lines):
        m = re.match(r"^(\s*)```(\S*)\s*(.*)$", lines[i])
        if not m:
            i += 1
            continue
        indent, start = len(m.group(1)), i
        i += 1
        body = []
        while i < len(lines) and not lines[i].strip().startswith("```"):
            body.append(lines[i][indent:] if lines[i][:indent].strip() == "" else lines[i])
            i += 1
        found.append((start + 1, m.group(2), m.group(3).split(), "\n".join(body) + "\n"))
        i += 1
    return found


def players_in(namespace, module):
    from opense4 import ai
    return [v for v in namespace.values()
            if isinstance(v, type) and issubclass(v, ai.Player) and v is not ai.Player and v.__module__ == module]


def play(player, fixtures):
    """The planning calls of a turn on the fixtures' view; the errors they gave."""
    from opense4 import testing
    services = testing.FakeServices(rules=fixtures.get("rules"))
    h = testing.Harness(player, services)
    errors = []
    for call in ("politics", "orders", "economy"):
        r = h.call(call, view=fixtures["view"])
        if r.get("error"):
            errors.append(call + ": " + str(r["error"]))
    h.call("end_session")
    return errors


def check(path, fixtures):
    """The problems of one file's Python blocks, how many blocks were checked and how many
    players were played."""
    problems, checked, played = [], 0, 0
    rel = os.path.relpath(path, ROOT)
    for n, (line, language, words, code) in enumerate(blocks(path)):
        if language != "python":
            continue
        where = "%s:%d" % (rel, line)
        kind = words[0] if words else ""
        if kind not in KINDS or len(words) > 1:
            problems.append(where + ": unknown words after ```python: " + " ".join(words) + " (none, no-run or fragment)")
            continue
        if kind == "fragment":
            continue
        checked += 1
        try:
            compiled = compile(code, where, "exec")
        except SyntaxError as e:
            problems.append("%s: does not compile: %s (line %s of the block)" % (where, e.msg, e.lineno))
            continue
        if kind == "no-run":
            continue
        module = "doc_snippet_%d" % n
        namespace = {"__name__": module}
        try:
            exec(compiled, namespace)
        except Exception:
            problems.append(where + ": fails when run:\n" + traceback.format_exc(limit=-3))
            continue
        if fixtures is None:
            continue
        for player in players_in(namespace, module):
            played += 1
            for error in play(player, fixtures):
                problems.append("%s: %s fails a planning call: %s" % (where, player.__name__, error))
    return problems, checked, played


def main(argv):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--fixtures", help="the fixtures of tests/sdk/test_sdk_python.cpp (a JSON file)")
    parser.add_argument("files", nargs="*", help="Markdown files (default: the SDK's documentation)")
    args = parser.parse_args(argv)
    sys.path.insert(0, os.path.join(ROOT, "python"))
    fixtures = None
    if args.fixtures:
        with open(args.fixtures, encoding="utf-8") as f:
            fixtures = json.load(f)
    problems, checked, played = [], 0, 0
    for path in args.files or markdown_files():
        p, c, n = check(os.path.abspath(path), fixtures)
        problems += p
        checked += c
        played += n
    for p in problems:
        print(p)
    print("%d Python examples checked, %d computer players played, %d problems" % (checked, played, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
