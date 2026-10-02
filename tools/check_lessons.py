#!/usr/bin/env python3
"""Render every step of every built-in tutorial with the input lock on.

For each step this runs, headless,

    opense4 --tutorial=<slug>:<n> --lesson-check [--screenshot=<dir>/<slug>-<n>.png]

which starts the lesson at that step, opens the windows the step works in,
and after a few frames prints one line:

    lesson-check <slug>:<n> areas=<k> ok|missing: <tags> [situational: <tags>] [under-panel: <tags>]

"missing" tags (highlighted or allowed, but not on screen) fail the check.
"situational" ones depend on the moment (a piece selected in battle, an
empire to talk to) and "under-panel" ones are outlines whose middle the
lesson panel covers: both only warn.

The installed game data must be found (as for opense4 itself). Screenshots
are written only when --screenshots names a directory; keep them out of the
repository (docs/CLEANROOM.md).

    python3 tools/check_lessons.py [--exe build/debug/opense4] [--screenshots DIR] [slug ...]
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
LINE = re.compile(r"^lesson-check (\S+):(\d+) areas=(\d+) (.*)$")


def tutorials(only):
    for path in sorted((ROOT / "assets" / "learn" / "tutorials").glob("*.toml")):
        slug = re.sub(r"^\d+-", "", path.stem)
        if only and slug not in only:
            continue
        with path.open("rb") as f:
            steps = tomllib.load(f).get("step", [])
        yield slug, steps


def check(exe, slug, n, shots, env):
    args = [str(exe), f"--tutorial={slug}:{n}", "--lesson-check"]
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(ROOT / "build" / "debug" / "opense4"))
    ap.add_argument("--screenshots", help="directory for one screenshot per step (outside the repository)")
    ap.add_argument("slugs", nargs="*", help="only these tutorials")
    a = ap.parse_args()

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

    failed = total = 0
    for slug, steps in tutorials(set(a.slugs)):
        for n in range(1, len(steps) + 1):
            total += 1
            ok, what = check(exe, slug, n, shots, env)
            title = steps[n - 1].get("title", "")
            print(f"{'ok  ' if ok else 'FAIL'} {slug}:{n} {title}: {what}")
            failed += 0 if ok else 1
    print(f"{total - failed} of {total} steps ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
