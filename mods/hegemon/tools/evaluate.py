#!/usr/bin/env python3
"""Plays Hegemon against the built-in AI with the SDK's arena (`opense4-sdk arena`,
docs/sdk/bots-and-arena.md) and summarises the runs: win rates with Wilson 95 %
confidence intervals, Hegemon's final score, colonies, tech levels and ships against the
best built-in AI of each game, and curves over time.

  python3 mods/hegemon/tools/evaluate.py run --suite=full --out=DIR [--jobs=4]
  python3 mods/hegemon/tools/evaluate.py summary DIR [--curves]

A suite is a list of configurations; each is one arena run (its own folder under DIR)
of N games between Hegemon and built-in AIs, game i on seed S + i with the players
moved i seats on, so that over a run every player plays every seat. Each command the
script runs is printed, so any run can be repeated by hand.
"""

import argparse
import csv
import math
import os
import subprocess
import sys

HEGEMON = "opense4.hegemon:Hegemon"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
MOD = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# name: (built-in opponents, systems, turn-based, turns, first seed, games)
SUITES = {
    "quick": {
        "duel-sim-30": (1, 30, False, 100, 1000, 8),
    },
    "full": {
        "duel-sim-30": (1, 30, False, 150, 2000, 24),
        "duel-tb-30": (1, 30, True, 150, 2100, 24),
        "duel-sim-50": (1, 50, False, 150, 2200, 16),
        "ffa4-sim-40": (3, 40, False, 150, 2300, 32),
        "ffa4-tb-40": (3, 40, True, 150, 2400, 24),
        "ffa6-sim-60": (5, 60, False, 150, 2500, 18),
        "ffa6-tb-60": (5, 60, True, 150, 2600, 18),
    },
    "baseline": {
        "base-duel-sim-30": (1, 30, False, 150, 2000, 24),
        "base-ffa4-sim-40": (3, 40, False, 150, 2300, 32),
        "base-ffa6-sim-60": (5, 60, False, 150, 2500, 18),
    },
}


def sdk():
    for p in (os.path.join(ROOT, "build", "release", "opense4-sdk"), os.path.join(ROOT, "build", "debug", "opense4-sdk")):
        if os.path.exists(p):
            return p
    sys.exit("opense4-sdk is not built (cmake --build --preset release --target opense4-sdk)")


def arena_command(name, cfg, out, jobs, turns=0, baseline=False):
    opponents, systems, turn_based, n_turns, seed, games = cfg
    players = ["builtin"] * (opponents + 1) if baseline else [HEGEMON] + ["builtin"] * opponents
    cmd = [sdk(), "arena", "--mod=" + MOD]
    cmd += ["--ai=" + p for p in players]
    cmd += ["--games=%d" % games, "--turns=%d" % (turns or n_turns), "--seed=%d" % seed, "--systems=%d" % systems,
            "--jobs=%d" % jobs, "--out=" + out]
    if turn_based:
        cmd.append("--turn-based")
    return cmd


def run(args):
    os.makedirs(args.out, exist_ok=True)
    for name, cfg in SUITES[args.suite].items():
        if args.only and args.only not in name:
            continue
        out = os.path.join(args.out, name)
        if os.path.exists(os.path.join(out, "games.csv")) and not args.again:
            print("%s: done already" % name)
            continue
        cmd = arena_command(name, cfg, out, args.jobs, args.turns, args.suite == "baseline")
        print("$ " + " ".join(cmd), flush=True)
        with open(out + ".log", "w") as log:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=log, text=True)
        tail = [l for l in r.stdout.splitlines() if l.strip()][-4:]
        print("\n".join(tail), flush=True)
        if r.returncode != 0:
            print("%s: the arena failed (%d), see %s.log" % (name, r.returncode, out))


def wilson(k, n, z=1.96):
    if n == 0:
        return (0.0, 0.0, 0.0)
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (p, max(0.0, c - h), min(1.0, c + h))


def mean_ci(xs, z=1.96):
    n = len(xs)
    if n == 0:
        return (0.0, 0.0)
    m = sum(xs) / n
    if n < 2:
        return (m, 0.0)
    v = sum((x - m) ** 2 for x in xs) / (n - 1)
    return (m, z * math.sqrt(v / n))


def games_of(folder):
    rows = []
    with open(os.path.join(folder, "games.csv")) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    games = {}
    for r in rows:
        games.setdefault(r["game"], []).append(r)
    return games


def subject_of(games):
    """The player measured: Hegemon, or in a baseline run the first built-in AI."""
    for rows in games.values():
        if any(r["ai"] == HEGEMON for r in rows):
            return HEGEMON
    return "builtin"


def summary(args):
    folders = []
    for d in args.dirs:
        if os.path.exists(os.path.join(d, "games.csv")):
            folders.append(d)
        else:
            for name in sorted(os.listdir(d)):
                p = os.path.join(d, name)
                if os.path.exists(os.path.join(p, "games.csv")):
                    folders.append(p)
    if not folders:
        sys.exit("no arena runs")
    keys = ("score", "colonies", "tech_levels", "ships", "research")
    print("%-18s %5s %4s %18s %18s %10s %10s %10s %10s %10s %5s %5s %7s" % (
        "config", "games", "fair", "wins", "survives", "score x", "colon x", "techs x", "research x", "ships x",
        "fails", "elim", "ms/turn"))
    all_games = []
    for folder in folders:
        name = os.path.basename(folder.rstrip("/"))
        games = games_of(folder)
        all_games.append((name, games))
        print_row(name, games, keys, ms_per_turn(folder))
    if len(folders) > 1:
        merged = {}
        for name, games in all_games:
            for g, rows in games.items():
                merged[name + "/" + g] = rows
        print_row("ALL", merged, keys, None)
    if args.curves:
        for folder in folders:
            curves(folder)


def ms_per_turn(folder):
    try:
        with open(os.path.join(folder, "report.csv")) as f:
            for r in csv.DictReader(f):
                if r["ai"] == HEGEMON:
                    return float(r["player_ms_per_turn"])
    except (OSError, KeyError, ValueError):
        pass
    return None


def print_row(name, games, keys, ms):
    """One line: the subject's wins and survival (Wilson 95 % intervals), its final
    figures over the best other player's (mean ± 95 % interval), the failed or
    replaced requests, the games in which some other empire was eliminated."""
    n = wins = alive = fails = elim = 0
    seats = 0
    ratios = {k: [] for k in keys}
    subject = subject_of(games)
    for g, rows in games.items():
        heg = [r for r in rows if r["ai"] == subject]
        others = [r for r in rows if r["ai"] != subject]
        if not heg:
            continue
        seats = len(rows)
        h = heg[0]
        n += 1
        wins += int(h["won"])
        alive += int(h["alive"])
        fails += int(h["failures"]) + int(h["fallbacks"])
        elim += 1 if any(r["eliminated"] for r in others) else 0
        for k in keys:
            best = max([float(r[k]) for r in others] + [1.0])
            ratios[k].append(float(h[k]) / best)
    if n == 0:
        return
    p, lo, hi = wilson(wins, n)
    s, slo, shi = wilson(alive, n)
    cells = []
    for k in ("score", "colonies", "tech_levels", "research", "ships"):
        m, ci = mean_ci(ratios[k])
        cells.append("%5.2f±%.2f" % (m, ci))
    print("%-18s %5d %3.0f%% %4.0f%% (%3.0f-%3.0f%%) %4.0f%% (%3.0f-%3.0f%%) %s %5d %5d %7s" % (
        name, n, 100.0 / max(1, seats), 100 * p, 100 * lo, 100 * hi, 100 * s, 100 * slo, 100 * shi, " ".join(cells),
        fails, elim, "%.1f" % ms if ms is not None else "-"))


def curves(folder):
    """Means over the run's games, every 10 turns: the subject, the built-in AIs'
    average and the best built-in AI of each game (by that turn's value)."""
    import glob
    import json
    cols = ("score", "colonies", "ships", "tech_levels", "research")
    sums = {}
    count = {}
    subject = None
    for path in sorted(glob.glob(os.path.join(folder, "games", "game-*.result.json"))):
        with open(path) as f:
            r = json.load(f)
        seats = r["seats"]
        if subject is None:
            subject = HEGEMON if any(x["ai"] == HEGEMON for x in seats) else "builtin"
        me = [x for x in seats if x["ai"] == subject]
        others = [x for x in seats if x["ai"] != subject]
        if not me or not others:
            continue
        me = me[0]
        n_turns = len(me["series"]["score"])
        for t in range(10, 151, 10):
            if t > n_turns:
                break
            for c in cols:
                def at(x):
                    v = x["series"].get(c) or []
                    return float(v[min(t, len(v)) - 1]) if v else 0.0
                vals = [at(x) for x in others]
                k = (t, c)
                a = sums.setdefault(k, [0.0, 0.0, 0.0])
                a[0] += at(me)
                a[1] += sum(vals) / len(vals)
                a[2] += max(vals)
                count[k] = count.get(k, 0) + 1
    print("\n%s: means per game, %s / built-in average / best built-in" % (os.path.basename(folder.rstrip("/")),
                                                                         "Hegemon" if subject == HEGEMON else "builtin"))
    print("turn " + " ".join("%26s" % c for c in cols))
    for t in range(10, 151, 10):
        if (t, "score") not in count:
            continue
        cells = []
        for c in cols:
            a = sums[(t, c)]
            n = count[(t, c)]
            fmt = "%8.0f /%8.0f /%8.0f" if c in ("score", "research") else "%8.1f /%8.1f /%8.1f"
            cells.append(fmt % (a[0] / n, a[1] / n, a[2] / n))
        print("%4d %s" % (t, " ".join(cells)))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--suite", default="quick", choices=sorted(SUITES))
    r.add_argument("--out", required=True)
    r.add_argument("--jobs", type=int, default=4)
    r.add_argument("--only", default="")
    r.add_argument("--turns", type=int, default=0)
    r.add_argument("--again", action="store_true")
    s = sub.add_parser("summary")
    s.add_argument("dirs", nargs="+")
    s.add_argument("--curves", action="store_true")
    a = ap.parse_args()
    if a.cmd == "run":
        if a.jobs > 4:
            sys.exit("--jobs: at most 4")
        run(a)
    else:
        summary(a)


if __name__ == "__main__":
    main()
