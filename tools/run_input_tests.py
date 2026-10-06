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

Each script plays in the layout its options give (most at 1024x768). Scripts
marked "# layouts: both" (the tutorials and most lesson scripts) work in the
original's small 800x600 layout too: --small plays them there as well, and
--only-small plays only those runs. Names may be patterns ("tutorial-*").

Screenshots that scripts take, and the picture of a failed step, go to --output
(by default a folder under the system's temporary folder): they show the game's
art, so keep them out of the repository (docs/CLEANROOM.md).

A script whose header has "# server: ARGS" plays against a dedicated host: the
runner starts opense4-server (beside the client) with ARGS on a free port of
this computer, and the client joins its lobby (--open=multiplayer:join=...).

    python3 tools/run_input_tests.py [--exe build/debug/opense4] [--jobs N]
        [--renderer opengl] [--output DIR] [--small | --only-small] [script ...]
"""

import argparse
import concurrent.futures
import contextlib
import fnmatch
import os
import pathlib
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "tests" / "input"
RESULT = re.compile(r"^input-script (.+): (passed|FAILED)(.*)$")
FIXTURE_MARK = "# ci: fixture-data"
# Scripts that also play in the 800x600 layout (--small), and the options that make it.
BOTH_MARK = "# layouts: both"
SMALL = ("800x600", ["--layout=800x600", "--size=800x600"])


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


SERVER_MARK = "# server:"


def server_args(path):
    """The opense4-server options of a script's "# server:" header line, or None."""
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("#"):
            break
        if line.startswith(SERVER_MARK):
            return shlex.split(line[len(SERVER_MARK):])
    return None


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@contextlib.contextmanager
def dedicated_host(exe, args, classic_dir, user):
    """opense4-server with `args` on a free port, listening; yields the port."""
    server = exe.with_name("opense4-server.exe" if os.name == "nt" else "opense4-server")
    port = free_port()
    env = dict(os.environ)
    env["OPENSE4_USER_DIR"] = user
    cmd = [str(server), f"--port={port}", "--bind=127.0.0.1", "--no-upnp", "--no-lan-discovery"]
    if classic_dir:
        cmd.append(f"--data={pathlib.Path(classic_dir) / 'Data'}")
    log = open(pathlib.Path(user) / "server.log", "w", encoding="utf-8")
    proc = subprocess.Popen(cmd + args, env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and proc.poll() is None:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                if s.connect_ex(("127.0.0.1", port)) == 0:
                    break
            time.sleep(0.2)
        yield port
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


def marked(path, mark):
    """Whether a line of the script's header comments starts with the mark."""
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("#"):
            break
        if line.strip() == mark or line.startswith(mark + " "):
            return True
    return False


def run(exe, script, output, classic_dir, renderer, timeout, extra, layout=None):
    """Plays one script; `layout` is (name, options) for a run in another layout."""
    host = server_args(script)
    if host is None:
        return play(exe, script, output, classic_dir, renderer, timeout, extra, layout)
    with tempfile.TemporaryDirectory(prefix="opense4-script-server-") as user:
        with dedicated_host(exe, host, classic_dir, user) as port:
            return play(exe, script, output, classic_dir, renderer, timeout, extra + [f"--open=multiplayer:join=127.0.0.1:{port}"], layout)


def play(exe, script, output, classic_dir, renderer, timeout, extra, layout=None):
    with tempfile.TemporaryDirectory(prefix="opense4-script-user-") as user:
        env = dict(os.environ)
        env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
        env["OPENSE4_USER_DIR"] = user
        shots = output / (script.stem + (f"@{layout[0]}" if layout else ""))
        shots.mkdir(parents=True, exist_ok=True)
        args = [str(exe), f"--input-script={script}", f"--script-output={shots}", "--no-audio"]
        if classic_dir:
            args.append(f"--classic-dir={classic_dir}")
        if renderer:
            args.append(f"--renderer={renderer}")
        args += extra
        if layout:
            args += layout[1]   # the command line overrides the script's options
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
    ap.add_argument("--small", action="store_true", help=f"also play the scripts marked '{BOTH_MARK}' at 800x600")
    ap.add_argument("--only-small", action="store_true", help=f"only the 800x600 runs of the scripts marked '{BOTH_MARK}'")
    ap.add_argument("scripts", nargs="*", help="only these scripts (names or paths)")
    a = ap.parse_args()

    exe = pathlib.Path(a.exe)
    if not exe.exists():
        sys.exit(f"{exe} not found: build first")
    chosen = scripts(a.scripts)
    if a.fixture_data and not a.scripts:
        chosen = [s for s in chosen if FIXTURE_MARK in s.read_text(encoding="utf-8")]
    # The runs: each script in its own layout, and the marked ones at 800x600 with --small.
    runs = [] if a.only_small else [(s, None) for s in chosen]
    if a.small or a.only_small:
        runs += [(s, SMALL) for s in chosen if marked(s, BOTH_MARK)]
    if not runs:
        sys.exit(f"nothing to play: none of the scripts chosen is marked '{BOTH_MARK}'")

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

    small = sum(1 for _, layout in runs if layout)
    print(f"{len(runs)} run(s) of {len(chosen)} script(s){f' ({small} at 800x600)' if small else ''}, "
          f"{a.jobs} at a time; pictures in {output}")
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        jobs = {pool.submit(run, exe, s, output, classic_dir, a.renderer, a.timeout, a.extra, layout): layout for s, layout in runs}
        for job in concurrent.futures.as_completed(jobs):
            script, ok, summary, detail, seconds = job.result()
            name = script.stem + (f" @{jobs[job][0]}" if jobs[job] else "")
            if ok:
                print(f"ok   {name} {summary} {seconds:.1f} s")
            else:
                failed += 1
                print(f"FAIL {name} ({seconds:.1f} s)")
                for line in detail.splitlines():
                    print(f"     {line}")
            sys.stdout.flush()
    print(f"{len(runs) - failed} of {len(runs)} runs passed")
    if not a.output:
        if failed:
            print(f"failure pictures kept in {output}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
