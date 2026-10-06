#!/usr/bin/env python3
"""Plays Hegemon against the built-in AI with opense4-ai-eval (or, later, opense4-sdk
arena) and summarises the results: win rates with Wilson confidence intervals, score,
colony, production, research and fleet curves, and the players' script costs.

  python3 mods/hegemon/tools/evaluate.py run --suite=quick --out=DIR [--jobs=4]
  python3 mods/hegemon/tools/evaluate.py summary DIR/*.jsonl

A suite is a list of configurations (players, galaxy size, turn style, turns, seeds);
every galaxy is played once per seat, the players shifted a seat each time, so that
each player plays each position and race of it. Games run as separate processes
(the script runtime is one per process), at most --jobs at once.
"""

import argparse
import glob
import json
import math
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HEGEMON = "opense4.hegemon:Hegemon"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
MOD = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# (name, players, systems, style, turns, first seed, seeds)
SUITES = {
    "quick": [
        ("duel-sim", [HEGEMON, "builtin"], 30, "simultaneous", 100, 1000, 4),
    ],
    "full": [
        ("duel-sim-30", [HEGEMON, "builtin"], 30, "simultaneous", 150, 2000, 6),
        ("duel-tb-30", [HEGEMON, "builtin"], 30, "turn-based", 150, 2100, 6),
        ("duel-sim-50", [HEGEMON, "builtin"], 50, "simultaneous", 150, 2200, 4),
        ("ffa4-sim-40", [HEGEMON, "builtin", "builtin", "builtin"], 40, "simultaneous", 150, 2300, 3),
        ("ffa4-tb-40", [HEGEMON, "builtin", "builtin", "builtin"], 40, "turn-based", 150, 2400, 3),
        ("ffa6-sim-60", [HEGEMON, "builtin", "builtin", "builtin", "builtin", "builtin"], 60, "simultaneous", 150, 2500, 2),
        ("ffa6-tb-60", [HEGEMON, "builtin", "builtin", "builtin", "builtin", "builtin"], 60, "turn-based", 150, 2600, 2),
    ],
    "baseline": [
        ("base-duel-sim-30", ["builtin", "builtin"], 30, "simultaneous", 150, 2000, 6),
        ("base-ffa4-sim-40", ["builtin", "builtin", "builtin", "builtin"], 40, "simultaneous", 150, 2300, 3),
    ],
}


def tool():
    for p in (os.path.join(ROOT, "build", "release", "opense4-ai-eval"), os.path.join(ROOT, "build", "debug", "opense4-ai-eval")):
        if os.path.exists(p):
            return p
    sys.exit("opense4-ai-eval not built (cmake --build --preset release --target opense4-ai-eval)")


def run(args):
    os.makedirs(args.out, exist_ok=True)
    exe = tool()
    jobs = []
    for name, players, systems, style, turns, seed0, seeds in SUITES[args.suite]:
        if args.only and args.only not in name:
            continue
        for s in range(seeds):
            seed = seed0 + s
            out = os.path.join(args.out, "%s-%d.jsonl" % (name, seed))
            if os.path.exists(out) and not args.again:
                continue
            cmd = [exe, "--mod=" + MOD, "--players=" + ",".join(players), "--seed=%d" % seed, "--seeds=1", "--rotate",
                   "--turns=%d" % (args.turns or turns), "--systems=%d" % systems, "--style=" + style, "--out=" + out + ".part"]
            jobs.append((name, seed, out, cmd))

    def one(job):
        name, seed, out, cmd = job
        with open(out + ".log", "w") as log:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=log, text=True)
        if r.returncode == 0 and os.path.exists(out + ".part"):
            os.replace(out + ".part", out)
        print("%s seed %d: %s" % (name, seed, r.stdout.strip().replace("\n", " | ")), flush=True)
        return r.returncode

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        codes = list(pool.map(one, jobs))
    print("%d games run, %d failed" % (len(codes), sum(1 for c in codes if c != 0)))


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


def load(paths):
    games = []
    for p in paths:
        for f in glob.glob(p):
            with open(f) as fh:
                for line in fh:
                    line = line.strip()
                    if line:
                        g = json.loads(line)
                        g["file"] = os.path.basename(f)
                        games.append(g)
    return games


def config_of(g):
    n = len(g["empires"])
    kind = "duel" if n == 2 else "ffa%d" % n
    return "%s %s %d sys" % (kind, g["style"], g["systems"])


def summary(args):
    games = load(args.files)
    if not games:
        sys.exit("no games")
    groups = {}
    for g in games:
        groups.setdefault(config_of(g), []).append(g)
    groups["ALL"] = games
    print("%-32s %5s %18s %18s %10s %10s %10s %8s %8s" % ("config", "games", "win (95% CI)", "survive", "score x", "colonies x", "prod x", "fails", "ms/turn"))
    for name in sorted(groups):
        gs = groups[name]
        wins = 0
        alive = 0
        n = 0
        ratios = {"score": [], "planets": [], "production": []}
        fails = 0
        ms = []
        for g in gs:
            heg = [e for e in g["empires"] if e["controller"] == HEGEMON]
            others = [e for e in g["empires"] if e["controller"] != HEGEMON]
            if not heg:
                continue
            h = heg[0]
            n += 1
            if g["winner"] == h["seat"]:
                wins += 1
            if h["alive"]:
                alive += 1
            for key in ratios:
                best = max([e[key] for e in others] + [1])
                ratios[key].append(h[key] / best)
            fails += h["failures"]
            ms.append(h["script_ms"] / max(1, g["turns"]))
        if n == 0:
            continue
        p, lo, hi = wilson(wins, n)
        s, slo, shi = wilson(alive, n)
        sc = mean_ci(ratios["score"])
        pl = mean_ci(ratios["planets"])
        pr = mean_ci(ratios["production"])
        print("%-32s %5d %5.0f%% (%3.0f-%3.0f%%) %5.0f%% (%3.0f-%3.0f%%) %5.2f±%.2f %5.2f±%.2f %5.2f±%.2f %8d %8.1f" % (
            name, n, 100 * p, 100 * lo, 100 * hi, 100 * s, 100 * slo, 100 * shi, sc[0], sc[1], pl[0], pl[1], pr[0], pr[1], fails,
            sum(ms) / len(ms)))
    if args.curves:
        curves(games)


def curves(games):
    """Mean of each statistic by turn: Hegemon against the best built-in AI of each game."""
    cols = ["score", "production", "research", "planets", "systems", "ships", "tonnage", "techs"]
    by_turn = {}
    for g in games:
        for e in g["empires"]:
            role = "hegemon" if e["controller"] == HEGEMON else "builtin"
            for row in e["curve"]:
                t = row[0]
                d = by_turn.setdefault(t, {"hegemon": [], "builtin": []})
                d[role].append(row)
    print("\nturn  " + "  ".join("%22s" % c for c in cols))
    for t in sorted(by_turn):
        d = by_turn[t]
        parts = []
        for i, c in enumerate(cols):
            def avg(rows):
                return sum(r[i + 2] for r in rows) / len(rows) if rows else 0
            parts.append("%10.0f / %-9.0f" % (avg(d["hegemon"]), avg(d["builtin"])))
        print("%4d  %s" % (t, "  ".join(parts)))


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
    s.add_argument("files", nargs="+")
    s.add_argument("--curves", action="store_true")
    a = ap.parse_args()
    if a.cmd == "run":
        run(a)
    else:
        summary(a)


if __name__ == "__main__":
    main()
