#!/usr/bin/env python3
"""Play every input script of tests/input against the client and report each one.

An input script (docs/BUILDING.md "Input scripts") drives the client through
its own input path: clicks, keys and checks, headless, with a fixed frame time.
Each script runs in a fresh user data folder, so no settings, saves or lesson
progress leak between runs or from your own.

The scripts play the player's installed game, so this is opt-in:

    OPENSE4_CLASSIC_DATA=auto python3 tools/run_input_tests.py      # the install found
    python3 tools/run_input_tests.py --install "/path/to/se4"       # that install

Without either it reports that it was skipped. --fixture-data runs the scripts
marked "# ci: fixture-data" on a game folder put together from our own test
fixtures instead (no original game needed; CI does this).

Screenshots that scripts take, and the picture of a failed step, go to --output
(by default a folder under the system's temporary folder): they show the game's
art, so keep them out of the repository (docs/CLEANROOM.md).

    python3 tools/run_input_tests.py [--exe build/debug/opense4] [--jobs N]
        [--renderer opengl] [--output DIR] [script ...]
"""

import argparse
import concurrent.futures
import fnmatch
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "tests" / "input"
RESULT = re.compile(r"^input-script (.+): (passed|FAILED)(.*)$")
FIXTURE_MARK = "# ci: fixture-data"


def scripts(names):
    found = sorted(SCRIPTS.glob("*.script"))
    if not names:
        return found
    out = []
    for name in names:
        p = pathlib.Path(name)
        if p.suffix == ".script" and p.exists():
            out.append(p.resolve())
            continue
        stem = pathlib.Path(name).stem
        if any(c in stem for c in "*?["):   # a pattern: "tutorial-*"
            match = [s for s in found if fnmatch.fnmatchcase(s.stem, stem)]
        else:
            match = [s for s in found if s.stem == name or s.stem == stem]
        if not match:
            sys.exit(f"no script named {name} in {SCRIPTS}")
        out.extend(match)
    return out


def lint(path):
    """The rules of the tutorial scripts: every step done as the lesson asks."""
    problems = []
    if not path.name.startswith("tutorial-"):
        return problems
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        code = line.split(" #", 1)[0].strip()
        if code.startswith("#"):
            continue
        if re.search(r"\bSkip\b", code) or "free-play" in code or "##next" in code:
            problems.append(f"{path.name}:{n}: tutorial scripts never press Skip or Free Play (click item:Next / item:Finish)")
    return problems


def fixture_game(where):
    """A game folder from our own fixtures: the minimal data set and a few pictures."""
    game = where / "fixture-game"
    shutil.copytree(ROOT / "tests" / "fixtures" / "minimal_dataset", game / "Data")
    shutil.copytree(ROOT / "tests" / "fixtures" / "install" / "Pictures", game / "Pictures")
    return game


def run(exe, script, output, classic_dir, renderer, timeout, extra):
    with tempfile.TemporaryDirectory(prefix="opense4-script-user-") as user:
        env = dict(os.environ)
        env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
        env["OPENSE4_USER_DIR"] = user
        shots = output / script.stem
        shots.mkdir(parents=True, exist_ok=True)
        args = [str(exe), f"--input-script={script}", f"--script-output={shots}", "--no-audio"]
        if classic_dir:
            args.append(f"--classic-dir={classic_dir}")
        if renderer:
            args.append(f"--renderer={renderer}")
        args += extra
        start = time.monotonic()
        try:
            proc = subprocess.run(args, env=env, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return script, False, f"timed out after {timeout} s", "", time.monotonic() - start
        seconds = time.monotonic() - start
        lines = proc.stdout.splitlines()
        result = next((m for m in (RESULT.match(l) for l in lines) if m), None)
        if result and result.group(2) == "passed" and proc.returncode == 0:
            return script, True, result.group(3).strip(), "", seconds
        # The failure: the lines after the verdict, or the end of the log.
        if result:
            at = next(i for i, l in enumerate(lines) if RESULT.match(l))
            detail = "\n".join(lines[at + 1 :])
        else:
            tail = (proc.stderr or proc.stdout).strip().splitlines()[-6:]
            detail = f"exit {proc.returncode}, no verdict:\n" + "\n".join(tail)
        return script, False, "", detail, seconds


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(ROOT / "build" / "debug" / ("opense4.exe" if os.name == "nt" else "opense4")))
    ap.add_argument("--install", help="the installed game to play (else OPENSE4_CLASSIC_DATA)")
    ap.add_argument("--fixture-data", action="store_true", help=f"the scripts marked '{FIXTURE_MARK}', on our own fixture data")
    ap.add_argument("--output", help="folder for screenshots and failure pictures (outside the repository)")
    ap.add_argument("--renderer", help="auto, vulkan or opengl (default: the client's)")
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)))
    ap.add_argument("--timeout", type=int, default=900, help="seconds a script may take")
    ap.add_argument("--extra", action="append", default=[], help="another option for the client (repeatable)")
    ap.add_argument("scripts", nargs="*", help="only these scripts (names or paths)")
    a = ap.parse_args()

    exe = pathlib.Path(a.exe)
    if not exe.exists():
        sys.exit(f"{exe} not found: build first")
    chosen = scripts(a.scripts)
    if a.fixture_data and not a.scripts:
        chosen = [s for s in chosen if FIXTURE_MARK in s.read_text(encoding="utf-8")]

    problems = [p for s in chosen for p in lint(s)]
    for p in problems:
        print(p)
    if problems:
        return 1

    work = pathlib.Path(tempfile.mkdtemp(prefix="opense4-input-tests-"))
    classic_dir = None
    if a.fixture_data:
        classic_dir = fixture_game(work)
    elif a.install:
        classic_dir = a.install
    else:
        data = os.environ.get("OPENSE4_CLASSIC_DATA", "")
        if not data:
            print("skipped: the input scripts play your installed game; set OPENSE4_CLASSIC_DATA=auto "
                  "(or a data folder), pass --install DIR, or use --fixture-data")
            return 0
        if data != "auto":
            classic_dir = data

    output = pathlib.Path(a.output).resolve() if a.output else work / "output"
    if output == ROOT or ROOT in output.parents:
        sys.exit("screenshots of the game's art stay out of the repository: pick a folder outside it")
    output.mkdir(parents=True, exist_ok=True)

    print(f"{len(chosen)} script(s), {a.jobs} at a time; pictures in {output}")
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        jobs = [pool.submit(run, exe, s, output, classic_dir, a.renderer, a.timeout, a.extra) for s in chosen]
        for job in concurrent.futures.as_completed(jobs):
            script, ok, summary, detail, seconds = job.result()
            if ok:
                print(f"ok   {script.stem} {summary} {seconds:.1f} s")
            else:
                failed += 1
                print(f"FAIL {script.stem} ({seconds:.1f} s)")
                for line in detail.splitlines():
                    print(f"     {line}")
            sys.stdout.flush()
    print(f"{len(chosen) - failed} of {len(chosen)} scripts passed")
    if not a.output:
        if failed:
            print(f"failure pictures kept in {output}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
