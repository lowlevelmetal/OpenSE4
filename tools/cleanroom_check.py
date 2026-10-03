#!/usr/bin/env python3
"""Clean-room lint: find passages in our files that match the original game's text.

Builds word n-gram fingerprints from the player's installed copy (manual pages,
data files, readme/history), then scans our docs, code and tests for runs of
N or more consecutive words that also appear there. Matches point at wording
that must be rewritten in our own words (see docs/CLEANROOM.md).

    tools/cleanroom_check.py [--install DIR] [--min-words N] [paths...]

Default paths: docs/ src/ tests/ assets/ packaging/ README.md. Exit status 1 if matches are found.

It also rejects traces of reverse-engineering output in tracked text: addresses in
the executable's range (eight hex digits starting with 004 or 005) and
decompiler-generated names (FUN_, DAT_, sub_ or fcn. followed by an address). Raw analysis output belongs in reference/re/.

Reviewed matches that are only functional identifiers (enum value lists, field
and setting names) can be listed, one lowercase snippet per line, in
tools/cleanroom_allow.txt; a match containing an allowed snippet is ignored.
"""

import argparse
import glob
import html
import os
import re
import sys

DEFAULT_INSTALLS = [
    "/mnt/games1/SteamLibrary/steamapps/common/Space Empires IV Deluxe/se4",
    os.path.expanduser("~/.local/share/Steam/steamapps/common/Space Empires IV Deluxe/se4"),
]

WORD = re.compile(r"[a-z0-9']+")


def words(text):
    return WORD.findall(text.lower())


def html_to_text(raw):
    t = re.sub(r"<(script|style)[^>]*>.*?</\1>", " ", raw, flags=re.S | re.I)
    t = re.sub(r"<[^>]+>", " ", t)
    return html.unescape(t)


def corpus_files(install):
    pats = ["Manual/**/*.htm", "Data/*.txt", "*.txt", "Ai/**/*.txt"]
    for pat in pats:
        yield from glob.glob(os.path.join(install, pat), recursive=True)


def fingerprints(install, n):
    grams = set()
    for path in corpus_files(install):
        raw = open(path, "rb").read().decode("latin-1")
        text = html_to_text(raw) if path.lower().endswith(".htm") else raw
        w = words(text)
        for i in range(len(w) - n + 1):
            grams.add(hash(tuple(w[i:i + n])))
    return grams


def free_text(text):
    """For "Key := Value" files keep only the values: field names and the
    format itself are shared on purpose (interoperability), prose is not."""
    if "*BEGIN*" not in text:
        return text
    out = []
    for line in text.splitlines():
        if ":=" in line:
            out.append(line.split(":=", 1)[1])
        elif not line.strip().startswith(("=", "*")):
            out.append(line)
        out.append(" | ")  # break runs between fields
    return "\n".join(out)


def scan(path, grams, n):
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return []
    structured = "*BEGIN*" in text
    text = free_text(text)
    w = [t for chunk in text.split(" | ") for t in words(chunk) + ["|"]]
    if not structured:
        w = [t for t in w if t != "|"]
    hits = ["|" not in w[i:i + n] and hash(tuple(w[i:i + n])) in grams for i in range(len(w) - n + 1)]
    runs, i = [], 0
    while i < len(hits):
        if hits[i]:
            j = i
            while j + 1 < len(hits) and hits[j + 1]:
                j += 1
            runs.append(" ".join(w[i:j + n]))
            i = j + 1
        else:
            i += 1
    return runs


# Address-like tokens (8 hex digits in the executable's 0x004xxxxx/0x005xxxxx range)
# and names that disassemblers and decompilers generate.
BINARY_TRACES = re.compile(
    r"\b(?:0x)?00[45][0-9a-fA-F]{5}\b|\b(?:thunk_)?(?:FUN|DAT|LAB|PTR|SUB|switchD|caseD)_[0-9a-fA-F]{6,8}\b"
    r"|\bsub_[0-9a-fA-F]{6,8}\b|\bfcn\.[0-9a-fA-F]{8}\b")


def binary_traces(files):
    found = 0
    for f in files:
        try:
            lines = open(f, encoding="utf-8", errors="replace").read().splitlines()
        except OSError:
            continue
        for i, line in enumerate(lines, 1):
            for m in BINARY_TRACES.finditer(line):
                found += 1
                print(f"{f}:{i}: looks like reverse-engineering output: {m.group(0)}")
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--install", help="original game directory (containing Data/ and Manual/)")
    ap.add_argument("--min-words", type=int, default=10)
    ap.add_argument("paths", nargs="*", default=["docs", "src", "tests", "assets", "packaging", "README.md"])
    args = ap.parse_args()

    install = args.install or next((d for d in DEFAULT_INSTALLS if os.path.isdir(os.path.join(d, "Data"))), None)
    if not install:
        print("original install not found; pass --install", file=sys.stderr)
        return 2
    grams = fingerprints(install, args.min_words)

    files = []
    for p in args.paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, f) for f in names if f.endswith((".md", ".txt", ".cpp", ".hpp", ".toml", ".py", ".xml", ".nsi", ".desktop", ".sh", ".in", ".script"))]
        elif os.path.exists(p):
            files.append(p)

    allow_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cleanroom_allow.txt")
    allowed = []
    if os.path.exists(allow_file):
        allowed = [l.strip() for l in open(allow_file) if l.strip() and not l.startswith("#")]

    found = 0
    for f in sorted(files):
        for run in scan(f, grams, args.min_words):
            if any(a in run for a in allowed):
                continue
            found += 1
            print(f"{f}: {len(run.split())} words: \"{run[:200]}\"")
    print(f"{found} matching passage(s) of {args.min_words}+ words in {len(files)} files")
    traces = binary_traces(files + [f for f in ("CLAUDE.md",) if os.path.exists(f)] +
                           [os.path.join(r, n) for r, _, ns in os.walk("tools") for n in ns if n.endswith((".py", ".cpp", ".hpp", ".md", ".txt"))])
    print(f"{traces} trace(s) of reverse-engineering output")
    return 1 if found or traces else 0


if __name__ == "__main__":
    sys.exit(main())
