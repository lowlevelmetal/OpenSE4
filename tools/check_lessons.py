#!/usr/bin/env python3
"""Check every step of every built-in tutorial: its length, and how it looks with the input lock on.

First the words of each step's text are counted, as the panel shows them
(a {design:<type>} token counts as one word): above 75 a step fails, above
60 it is reported (docs/LEARNING.md "Writing steps"; tests/test_learn.cpp
checks the same). --words stops there; it needs no game.

Then, for each step, this runs headless

    opense4 --tutorial=<slug>:<n> --lesson-check [--screenshot=<dir>/<slug>-<n>.png]

which starts the lesson at that step, opens the windows the step works in,
and after a few frames prints one line:

    lesson-check <slug>:<n> areas=<k> ok|missing: <tags> [situational: <tags>] [under-panel: <tags>]

"missing" tags (highlighted or allowed, but not on screen) fail the check.
"situational" ones depend on the moment (a piece selected in battle, an
empire to talk to, the options of a picker that is not open yet) and
"under-panel" ones are outlines whose middle the lesson panel covers: both
only warn. The parts a step shows (`show`) are checked like the others.

--audit runs each step with --lesson-audit instead, at 1024x768 and at the
original's 800x600 (docs/LEARNING.md "Checking the lessons"), and prints for
each step what the input lock lets through (A) and whether each thing its text
names can be seen (B): the flagged lines and the references that matched
nothing (all lines with --verbose), then a summary per tutorial. Flagged lines
are for a person to judge: the audit is a guide, the screenshots decide.

The installed game data must be found (as for opense4 itself). Screenshots
are written only when --screenshots names a directory; keep them out of the
repository (docs/CLEANROOM.md).

    python3 tools/check_lessons.py [--exe build/debug/opense4] [--screenshots DIR] [--words] [slug ...]
    python3 tools/check_lessons.py --audit [--jobs 4] [--verbose] [--screenshots DIR] [--learn-dir DIR] [slug ...]

The client runs with a user folder of its own (OPENSE4_USER_DIR, a temporary
one unless the environment names one), so your settings are never touched.
"""

import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
LINE = re.compile(r"^lesson-check (\S+):(\d+) areas=(\d+) (.*)$")
AUDIT = re.compile(r"^lesson-audit (\S+):(\d+) (.*)$")
AUDIT_END = re.compile(r"^end flags=(\d+) unmatched=(\d+)")
LAYOUTS = [("1024x768", ["--layout=1024x768", "--size=1280x800"]), ("800x600", ["--layout=800x600", "--size=800x600"])]
MAX_WORDS, WARN_WORDS = 75, 60
TOKEN = re.compile(r"\{[a-z-]+:[^{}\n]*\}")
LINK = re.compile(r"\[([^\]]*)\]\([^)]*\)")
MARKER = re.compile(r"^\s*(?:[-*]|\d+\.)\s+", re.M)


def words(text):
    """The words of a step's Markdown as the panel shows them."""
    text = TOKEN.sub("Name", text)
    text = LINK.sub(r"\1", text)
    text = MARKER.sub("", text)
    text = text.replace("**", "").replace("`", "").replace("|", " ")
    return len(text.split())


def check_words(only):
    """Prints each tutorial's words per step; returns the number of steps above the limit."""
    too_long = 0
    for slug, steps in tutorials(only):
        counts = [words(step.get("text", "")) for step in steps]
        total = sum(counts)
        print(f"words {slug}: {len(steps)} steps, {total} words, {total / max(1, len(steps)):.1f} a step, the longest {max(counts, default=0)}")
        for n, (step, count) in enumerate(zip(steps, counts), 1):
            if count > MAX_WORDS:
                too_long += 1
                print(f"FAIL {slug}:{n} {step.get('title', '')}: {count} words, more than {MAX_WORDS}")
            elif count > WARN_WORDS:
                print(f"warn {slug}:{n} {step.get('title', '')}: {count} words (aim for about 50)")
    return too_long


LEARN_DIR = None   # --learn-dir: the content to check instead of assets/learn


def tutorials(only):
    for path in sorted(((LEARN_DIR or ROOT / "assets" / "learn") / "tutorials").glob("*.toml")):
        slug = re.sub(r"^\d+-", "", path.stem)
        if only and slug not in only:
            continue
        with path.open("rb") as f:
            steps = tomllib.load(f).get("step", [])
        yield slug, steps


def learn_args():
    return [f"--learn-dir={LEARN_DIR}"] if LEARN_DIR else []


def check(exe, slug, n, shots, env):
    args = [str(exe), f"--tutorial={slug}:{n}", "--lesson-check"] + learn_args()
    if shots:
        args.append(f"--screenshot={shots / f'{slug}-{n:02d}.png'}")
    try:
        run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return False, "timed out"
    found = [m for m in (LINE.match(l) for l in run.stdout.splitlines()) if m]
    if not found:
        tail = (run.stderr or run.stdout).strip().splitlines()[-3:]
        return False, f"no lesson-check line (exit {run.returncode}): " + " | ".join(tail)
    m = found[-1]
    if int(m.group(2)) != n:
        return False, f"the lesson started at step {m.group(2)}, not {n}"
    rest = m.group(4)
    ok = run.returncode == 0 and rest.startswith("ok")
    return ok, f"areas={m.group(3)} {rest}"


def audit(exe, slug, n, layout, shots, env):
    """One step at one layout: (flags, unmatched, lines) or (None, None, [why])."""
    name, options = layout
    args = [str(exe), f"--tutorial={slug}:{n}", "--lesson-audit"] + options + learn_args()
    if shots:
        args.append(f"--screenshot={shots / f'{slug}-{n:02d}@{name}.png'}")
    try:
        run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None, None, ["timed out"]
    lines, flags, unmatched = [], None, None
    for raw in run.stdout.splitlines():
        m = AUDIT.match(raw)
        if not m or int(m.group(2)) != n:
            continue
        end = AUDIT_END.match(m.group(3))
        if end:
            flags, unmatched = int(end.group(1)), int(end.group(2))
        else:
            lines.append(m.group(3))
    if flags is None:
        tail = (run.stderr or run.stdout).strip().splitlines()[-3:]
        return None, None, ["no lesson-audit report (exit {}): {}".format(run.returncode, " | ".join(tail))]
    return flags, unmatched, lines


def run_audit(exe, only, shots, env, jobs, verbose):
    work = []
    for slug, steps in tutorials(only):
        for n in range(1, len(steps) + 1):
            for layout in LAYOUTS:
                work.append((slug, n, steps[n - 1].get("title", ""), layout))
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {pool.submit(audit, exe, slug, n, layout, shots, env): (slug, n, layout[0]) for slug, n, _, layout in work}
        for f in concurrent.futures.as_completed(futures):
            results[futures[f]] = f.result()
    summary = {}
    failed = 0
    for slug, n, title, layout in work:
        flags, unmatched, lines = results[(slug, n, layout[0])]
        row = summary.setdefault(slug, {"steps": set(), "1024x768": 0, "800x600": 0, "unmatched": 0, "clean": 0, "runs": 0})
        row["steps"].add(n)
        row["runs"] += 1
        if flags is None:
            failed += 1
            print(f"FAIL {slug}:{n} {title} @{layout[0]}: {lines[0]}")
            continue
        row[layout[0]] += flags
        row["unmatched"] += unmatched
        row["clean"] += 1 if flags == 0 and unmatched == 0 else 0
        print(f"audit {slug}:{n} {title} @{layout[0]}: {flags} flagged, {unmatched} unmatched")
        for l in lines:
            if verbose or "FLAG:" in l or "not matched" in l:
                print(f"    {l}")
    print()
    print(f"{'tutorial':34} {'steps':>5} {'flags@1024x768':>15} {'flags@800x600':>14} {'unmatched':>10} {'clean runs':>11}")
    for slug, row in summary.items():
        print(f"{slug:34} {len(row['steps']):5} {row['1024x768']:15} {row['800x600']:14} {row['unmatched']:10} {row['clean']:6} of {row['runs']}")
    if failed:
        print(f"{failed} runs gave no report")
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(ROOT / "build" / "debug" / "opense4"))
    ap.add_argument("--screenshots", help="directory for one screenshot per step (outside the repository)")
    ap.add_argument("--words", action="store_true", help="only count the words of each step (no game needed)")
    ap.add_argument("--audit", action="store_true", help="audit each step at both layouts (--lesson-audit): what the lock lets through, what the text names")
    ap.add_argument("--jobs", type=int, default=4, help="--audit: runs at a time (default 4)")
    ap.add_argument("--verbose", action="store_true", help="--audit: print every line, not only the flagged and unmatched ones")
    ap.add_argument("--learn-dir", help="check the content of this folder (as the client's --learn-dir) instead of assets/learn")
    ap.add_argument("slugs", nargs="*", help="only these tutorials")
    a = ap.parse_args()
    global LEARN_DIR
    if a.learn_dir:
        LEARN_DIR = pathlib.Path(a.learn_dir).resolve()

    too_long = check_words(set(a.slugs))
    if a.words:
        return 1 if too_long else 0

    exe = pathlib.Path(a.exe)
    if not exe.exists():
        sys.exit(f"{exe} not found: build first")
    shots = None
    if a.screenshots:
        shots = pathlib.Path(a.screenshots).resolve()
        if ROOT in shots.parents or shots == ROOT:
            sys.exit("screenshots of the game's art stay out of the repository: pick a directory outside it")
        shots.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
    # A user folder of its own: the player's settings and saves are never touched.
    user = None
    if "OPENSE4_USER_DIR" not in env:
        user = tempfile.TemporaryDirectory(prefix="opense4-lessons-")
        env["OPENSE4_USER_DIR"] = user.name

    if a.audit:
        return run_audit(exe, set(a.slugs), shots, env, a.jobs, a.verbose) or (1 if too_long else 0)

    failed = total = 0
    for slug, steps in tutorials(set(a.slugs)):
        for n in range(1, len(steps) + 1):
            total += 1
            ok, what = check(exe, slug, n, shots, env)
            title = steps[n - 1].get("title", "")
            print(f"{'ok  ' if ok else 'FAIL'} {slug}:{n} {title}: {what}")
            failed += 0 if ok else 1
    print(f"{total - failed} of {total} steps ok")
    if too_long:
        print(f"{too_long} steps have more than {MAX_WORDS} words")
    return 1 if failed or too_long else 0


if __name__ == "__main__":
    sys.exit(main())
