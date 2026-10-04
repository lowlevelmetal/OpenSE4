#!/usr/bin/env python3
"""Play the audio scripts of tests/audio headless and check what the client sounded like.

Each script (an input script, docs/BUILDING.md "Input scripts", with --audio)
plays in a fresh user data folder with SDL's disk audio driver, which writes
the mixed output to a file in real time instead of to a device. The check then
reads that file and fails on:

- clicks: a jump between neighbouring samples far above the sound around it
  (a sound started or stopped without a fade, a track broken off);
- hard edges: digital silence that begins or ends away from zero;
- music missing: three seconds or more in a row without music (as when a track
  played once and never started again);
- clipping: samples at full scale;
- the log (opense4.log) reporting a track or sound that could not be played,
  or music that ran dry.

The scripts play the player's installed game, so this is opt-in:

    OPENSE4_CLASSIC_DATA=auto python3 tools/check_audio.py
    python3 tools/check_audio.py --install "/path/to/se4" [--keep DIR] [script ...]

A script may ask for the device's format in its header ("# device: F32 2 48000",
as Windows' shared mode usually mixes); otherwise the disk driver's own (16-bit
stereo at 44.1 kHz). Each run takes about a minute of real time (the audio is real time); they run
side by side. --keep keeps the raw audio, the logs and a report per script.
"""

import argparse
import array
import concurrent.futures
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "tests" / "audio"
RESULT = re.compile(r"^input-script (.+): (passed|FAILED)(.*)$")
DEVICE = re.compile(r"Audio: .* through disk: (\d+) Hz, (\d+) channels, SDL_AUDIO_(\w+)")
# What SDL's disk driver itself prints (stderr), should the log not say.
DISK = re.compile(r"format=(\w+) channels=(\d+) freq=(\d+)")

# A click: the second difference of a channel above CLICK_LEVEL and CLICK_RATIO
# times its level in the 20 ms around it. Music and the game's interface sounds
# stay below 0.03; a sound broken off mid-wave jumps by up to twice its level.
CLICK_LEVEL = 0.03
CLICK_RATIO = 12.0
# Music is missing in a second whose level is below -60 dBFS.
SILENT_DB = -60.0
MAX_SILENT_SECONDS = 3


def samples(path, fmt, channels):
    data = path.read_bytes()
    if fmt.startswith("F32"):
        a = array.array("f")
        a.frombytes(data[: len(data) // 4 * 4])
        values = a
    elif fmt.startswith("S16"):
        a = array.array("h")
        a.frombytes(data[: len(data) // 2 * 2])
        values = [v / 32768.0 for v in a]
    elif fmt.startswith("S32"):
        a = array.array("i")
        a.frombytes(data[: len(data) // 4 * 4])
        values = [v / 2147483648.0 for v in a]
    else:
        raise ValueError(f"unknown sample format {fmt}")
    return [values[c::channels] for c in range(channels)]


def analyse(chans, rate, start):
    """The problems of a capture, and a summary line."""
    problems = []
    n = len(chans[0])
    first = start
    # Before the first sound there is only the device starting: skip it.
    while first < n and all(ch[first] == 0.0 for ch in chans):
        first += 1
    if first >= n:
        return ["no sound at all"], "silent"

    # Loudness per second: music missing.
    levels = []
    for s in range(first // rate, n // rate):
        seg = chans[0][s * rate:(s + 1) * rate]
        rms = math.sqrt(sum(v * v for v in seg) / len(seg))
        levels.append(-200.0 if rms == 0 else 20 * math.log10(rms))
    run = 0
    for i, db in enumerate(levels[1:], 1):   # the first second may start mid-way
        run = run + 1 if db < SILENT_DB else 0
        if run == MAX_SILENT_SECONDS:
            problems.append(f"no music for {MAX_SILENT_SECONDS} s or more from {first / rate + i - run + 1:.0f} s")

    # Hard edges into and out of digital silence (2 ms or more).
    peak_abs = lambda i: max(abs(ch[i]) for ch in chans)
    i, edges = first, 0
    while i < n:
        if all(ch[i] == 0.0 for ch in chans):
            j = i
            while j < n and all(ch[j] == 0.0 for ch in chans):
                j += 1
            if j - i >= rate // 500 and j < n:
                before, after = peak_abs(i - 1), peak_abs(j)
                if before > 0.01 or after > 0.01:
                    edges += 1
                    if edges <= 5:
                        problems.append(f"hard edge at {i / rate:.3f} s ({before:.3f} into silence, {after:.3f} out of it)")
            i = j
        else:
            i += 1

    # Clicks.
    win = rate // 100
    clicks = []
    for c, x in enumerate(chans):
        d2 = [0.0, 0.0] + [abs(x[k] - 2 * x[k - 1] + x[k - 2]) for k in range(2, n)]
        blocks = [sum(d2[b:b + win]) / win for b in range(0, n, win)]
        k = max(first, 2)
        while k < n:
            v = d2[k]
            if v > CLICK_LEVEL:
                b = k // win
                around = sorted(blocks[j] for j in (b - 2, b - 1, b + 1, b + 2) if 0 <= j < len(blocks))
                local = around[len(around) // 2] if around else 0.0
                if v > CLICK_RATIO * local + CLICK_LEVEL:
                    clicks.append((k / rate, c, v))
                    k += win
                    continue
            k += 1
    # One click per moment, whichever channel jumped most.
    clicks.sort()
    merged = []
    for t, c, v in clicks:
        if merged and t - merged[-1][0] < 0.01:
            if v > merged[-1][2]:
                merged[-1] = (t, c, v)
            continue
        merged.append((t, c, v))
    clicks = merged
    for t, c, v in sorted(clicks, key=lambda t: -t[2])[:5]:
        problems.append(f"click at {t:.3f} s (channel {c}): a jump of {v:.3f}")
    if len(clicks) > 5:
        problems.append(f"... {len(clicks) - 5} more clicks")

    clipped = sum(1 for k in range(first, n) if peak_abs(k) >= 0.9999)
    if clipped:
        problems.append(f"{clipped} samples at full scale (clipping)")
    loud = sum(1 for db in levels if db >= SILENT_DB)
    summary = (f"{(n - first) / rate:.1f} s of sound, music in {loud} of {len(levels)} s, "
               f"{len(clicks)} clicks, peak {max(peak_abs(k) for k in range(first, n)):.2f}")
    return problems, summary


def device_format(script):
    """The format a script's "# device: FORMAT CHANNELS RATE" line asks of the disk driver."""
    for line in script.read_text(encoding="utf-8").splitlines():
        if not line.startswith("#"):
            break
        if line.startswith("# device:"):
            fmt, channels, rate = line.split(":", 1)[1].split()
            return {"SDL_AUDIO_FORMAT": fmt, "SDL_AUDIO_CHANNELS": channels, "SDL_AUDIO_FREQUENCY": rate}
    return {}


def run(exe, script, keep, classic_dir, renderer, timeout):
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"opense4-audio-{script.stem}-"))
    user = work / "user"
    user.mkdir()
    # Real-time frames: the scripts' waits are frames and the audio is real time.
    (user / "settings.toml").write_text("[graphics]\nvsync = false\nframe_limit = 60\n", encoding="utf-8")
    raw = work / f"{script.stem}.raw"
    env = dict(os.environ)
    env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
    env["SDL_AUDIO_DRIVER"] = "disk"
    env["SDL_AUDIO_DISK_OUTPUT_FILE"] = str(raw)
    env["OPENSE4_USER_DIR"] = str(user)
    env.update(device_format(script))
    args = [str(exe), f"--input-script={script}", f"--script-output={work}", "--audio"]
    if classic_dir:
        args.append(f"--classic-dir={classic_dir}")
    if renderer:
        args.append(f"--renderer={renderer}")
    start = time.monotonic()
    problems = []
    stderr = ""
    try:
        proc = subprocess.run(args, env=env, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        stderr = proc.stderr
        lines = proc.stdout.splitlines()
        verdict = next((m for m in (RESULT.match(l) for l in lines) if m), None)
        if not verdict or verdict.group(2) != "passed" or proc.returncode != 0:
            tail = (proc.stdout + proc.stderr).strip().splitlines()[-6:]
            problems.append("the script failed:\n       " + "\n       ".join(tail))
    except subprocess.TimeoutExpired:
        problems.append(f"timed out after {timeout} s")
    seconds = time.monotonic() - start
    log_file = user / "opense4.log"
    log = log_file.read_text(encoding="utf-8", errors="replace") if log_file.exists() else ""
    summary = ""
    if device := DEVICE.search(log):
        rate, channels, fmt = int(device.group(1)), int(device.group(2)), device.group(3)
    elif disk := DISK.search(stderr):
        rate, channels, fmt = int(disk.group(3)), int(disk.group(2)), disk.group(1)
        problems.append("the log names no audio device")
    else:
        rate = 0
        problems.append("no disk audio device was opened")
    if rate and not raw.exists():
        problems.append("no audio was written")
    elif rate:
        found, summary = analyse(samples(raw, fmt, channels), rate, 0)
        problems += found
    for line in log.splitlines():
        missing = "Not in the installed game" in line and ("Sounds/" in line or "Music/" in line)
        if "cannot play" in line or "ran dry" in line or missing:
            problems.append("log: " + line.strip())
    if "Music: playing" not in log:
        problems.append("log: no track started")
    if keep:
        dest = keep / script.stem
        shutil.rmtree(dest, ignore_errors=True)
        shutil.copytree(work, dest)
        (dest / "report.txt").write_text(summary + "\n" + "\n".join(problems) + "\n", encoding="utf-8")
    shutil.rmtree(work, ignore_errors=True)
    return script, problems, summary, seconds


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(ROOT / "build" / "debug" / ("opense4.exe" if os.name == "nt" else "opense4")))
    ap.add_argument("--install", help="the installed game to play (else OPENSE4_CLASSIC_DATA)")
    ap.add_argument("--renderer", help="auto, vulkan or opengl (default: the client's)")
    ap.add_argument("--keep", help="keep each run's audio, log and report in this folder (outside the repository)")
    ap.add_argument("--timeout", type=int, default=600, help="seconds a script may take")
    ap.add_argument("scripts", nargs="*", help="only these scripts (names or paths)")
    a = ap.parse_args()

    exe = pathlib.Path(a.exe)
    if not exe.exists():
        sys.exit(f"{exe} not found: build first")
    chosen = sorted(SCRIPTS.glob("*.script"))
    if a.scripts:
        chosen = [s for s in chosen if s.stem in {pathlib.Path(n).stem for n in a.scripts}]
        if not chosen:
            sys.exit(f"no such script in {SCRIPTS}")
    classic_dir = a.install
    if not classic_dir:
        data = os.environ.get("OPENSE4_CLASSIC_DATA", "")
        if not data:
            print("skipped: the audio scripts play your installed game; set OPENSE4_CLASSIC_DATA=auto "
                  "(or a data folder) or pass --install DIR")
            return 0
        classic_dir = None if data == "auto" else data
    keep = pathlib.Path(a.keep).resolve() if a.keep else None
    if keep:
        if keep == ROOT or ROOT in keep.parents:
            sys.exit("the captures hold the game's music: keep them outside the repository")
        keep.mkdir(parents=True, exist_ok=True)

    print(f"{len(chosen)} audio script(s), side by side (about a minute each)")
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(chosen) or 1) as pool:
        for job in concurrent.futures.as_completed([pool.submit(run, exe, s, keep, classic_dir, a.renderer, a.timeout) for s in chosen]):
            script, problems, summary, seconds = job.result()
            if problems:
                failed += 1
                print(f"FAIL {script.stem} ({seconds:.0f} s) {summary}")
                for p in problems:
                    print(f"     {p}")
            else:
                print(f"ok   {script.stem} ({seconds:.0f} s) {summary}")
            sys.stdout.flush()
    print(f"{len(chosen) - failed} of {len(chosen)} audio scripts passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
