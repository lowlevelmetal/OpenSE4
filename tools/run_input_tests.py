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

A script whose header has "# portable-copy" plays a copy of the client in a
program folder of its own (a hard link beside the client where it can be),
without OPENSE4_USER_DIR, with a system user folder of its own that holds a
saved game; afterwards the runner checks that the copy became portable, that
the saved game was copied into its userdata folder and that it is still in
the system folder (docs/SETUP.md "A portable copy"). With "# portable-copy
read-only" the program folder cannot be written, and the runner checks that
nothing was made in it. Linux and the BSDs only: elsewhere the system's folder
cannot be moved for a run, and the script is skipped (also when run as root,
who writes anywhere, for a read-only one).

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


PORTABLE_MARK = "# portable-copy"
PORTABLE_SEED = b"OpenSE4 portable-copy check\n"


def portable_supported(read_only):
    if os.name == "nt" or sys.platform == "darwin":
        return False
    return not (read_only and os.geteuid() == 0)


def portable_read_only(path):
    """Whether the script's "# portable-copy" line asks for a program folder that cannot be written."""
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("#"):
            break
        if line.startswith(PORTABLE_MARK):
            return "read-only" in line[len(PORTABLE_MARK):].split()
    return False


@contextlib.contextmanager
def portable_copy(exe, read_only=False):
    """The client in a program folder of its own, and a home with a saved game in its user folder.

    Yields (the copied program, its environment changes, a check to run afterwards)."""
    with tempfile.TemporaryDirectory(prefix=".opense4-portable-run-", dir=exe.parent) as program, \
            tempfile.TemporaryDirectory(prefix="opense4-portable-home-") as home:
        program = pathlib.Path(program)
        copy = program / exe.name
        try:
            os.link(exe, copy)      # the same file: no copy of a large debug build
        except OSError:
            shutil.copy2(exe, copy)
        data = pathlib.Path(home) / "data"
        system = data / "OpenSE4"   # SDL's preference folder under XDG_DATA_HOME
        (system / "saves").mkdir(parents=True)
        (system / "saves" / "Seeded.gam").write_bytes(PORTABLE_SEED)
        # SDL's preference folder follows XDG_DATA_HOME; HOME stays, as the game
        # finds the Steam libraries through it.
        env = {"XDG_DATA_HOME": str(data)}
        if read_only:
            program.chmod(0o555)

        def check():
            problems = []
            if not (system / "saves" / "Seeded.gam").is_file():
                problems.append("the saved game is gone from the system folder")
            if read_only:
                made = sorted(p.name for p in program.iterdir() if p.name != exe.name)
                if made:
                    problems.append(f"files were made in the read-only program folder: {', '.join(made)}")
                return problems
            if not (program / "portable.txt").is_file():
                problems.append("no portable.txt beside the program")
            copied = program / "userdata" / "saves" / "Seeded.gam"
            if not copied.is_file() or copied.read_bytes() != PORTABLE_SEED:
                problems.append(f"the saved game was not copied to {copied}")
            if not (program / "userdata" / "settings.toml").is_file():
                problems.append("no settings.toml in the portable folder")
            return problems

        try:
            yield copy, env, check
        finally:
            program.chmod(0o755)   # so that the folder can be removed


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def server_exe(exe):
    return exe.with_name("opense4-server.exe" if os.name == "nt" else "opense4-server")


@contextlib.contextmanager
def dedicated_host(exe, args, classic_dir, user):
    """opense4-server with `args` on a free port, listening; yields the port."""
    server = server_exe(exe)
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
    if not server_exe(exe).exists():
        return script, False, "", f"{server_exe(exe)} not found: build it (the opense4-server target)", 0.0
    with tempfile.TemporaryDirectory(prefix="opense4-script-server-") as user:
        with dedicated_host(exe, host, classic_dir, user) as port:
            return play(exe, script, output, classic_dir, renderer, timeout, extra + [f"--open=multiplayer:join=127.0.0.1:{port}"], layout)


FREE_PORT = "{free_port}"


def play(exe, script, output, classic_dir, renderer, timeout, extra, layout=None):
    if marked(script, PORTABLE_MARK):
        read_only = portable_read_only(script)
        if not portable_supported(read_only):
            return script, True, "(skipped: a portable copy is checked on Linux, as a user other than root)", "", 0.0
        with portable_copy(exe, read_only) as (copy, env, check):
            result = play_one(copy, script, output, classic_dir, renderer, timeout, extra, layout, env)
            problems = check() if result[1] else []
            if problems:
                return script, False, "", "\n".join(problems), result[4]
            return result
    return play_one(exe, script, output, classic_dir, renderer, timeout, extra, layout)


def play_one(exe, script, output, classic_dir, renderer, timeout, extra, layout=None, portable_env=None):
    with tempfile.TemporaryDirectory(prefix="opense4-script-user-") as user:
        # A script that hosts a game types "{free_port}" where it needs a port: each
        # run gets its own, so runs played at the same time (both layouts) never meet.
        text = script.read_text(encoding="utf-8")
        if FREE_PORT in text:
            copy = pathlib.Path(user) / "script" / script.name
            copy.parent.mkdir()
            copy.write_text(text.replace(FREE_PORT, str(free_port())), encoding="utf-8")
            script = copy
        env = dict(os.environ)
        env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
        env["OPENSE4_USER_DIR"] = user
        if portable_env is not None:   # a portable copy: the rules without OPENSE4_USER_DIR
            del env["OPENSE4_USER_DIR"]
            env.update(portable_env)
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
