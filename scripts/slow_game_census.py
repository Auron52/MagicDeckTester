#!/usr/bin/env python3
"""Census of pathological (degenerate) games from a batch run's SLOW-GAME log.

WHY THIS EXISTS. The engine reports every game over MTG_SLOW_GAME_MS on stderr, and the depth
matrix tags each line with its cell and appends to `slow_games.log`. That file is the only DURABLE
record: the batch heartbeat keeps just the top 100 by duration and is a rolling snapshot, so the
evidence for "which games are degenerate" is gone once a long run turns the buffer over.

WHAT IT ANSWERS. A degenerate game is not merely slow -- it is slow in a way that should not be
possible. Two signatures separate it from "this game is legitimately big":

  * SAME GAME AT EVERY DEPTH. If one game index is among the worst at d1 AND d5, the cost is not
    coming from search depth -- it is the board state. A depth-1 game taking hours is the sharpest
    possible evidence, because almost no search machinery is involved at one ply.
  * UNIT-RATE COLLAPSE. Work units are the currency the per-game ceiling is denominated in. A game
    running far below the deck's normal units/s overshoots its ceiling in WALL time while landing
    exactly on it in units -- which is how a ceiling meant to bound an hour permits four.

Usage:
    python3 scripts/slow_game_census.py logs/vlq_fungus/slow_games.log [--top N]
"""
import argparse
import collections
import re
import sys

# `pool [goldfish] SLOW-GAME 62874ms  job=fungus_staged_V7_s8008_off150 gi=172 wt=7
#  units=2817708  repro: --seed 8180 --game-index 172 --games 1`
LINE = re.compile(
    r"SLOW-GAME\s+(?P<ms>\d+)ms\s+job=(?P<job>\S+)\s+gi=(?P<gi>-?\d+)\s+"
    r"wt=(?P<wt>-?\d+)\s+units=(?P<units>\d+).*?--seed\s+(?P<seed>\d+)\s+--game-index\s+(?P<gidx>-?\d+)")
# fungus_staged_H4_s10010_off50 -> arm H, depth 4, seed-base 10010
JOB = re.compile(r"_(?P<arm>[HV])(?P<depth>\d+)_s(?P<base>\d+)_off(?P<off>\d+)$")

INT_MIN = -2147483648


def parse(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            m = LINE.search(line)
            if not m:
                continue
            j = JOB.search(m.group("job"))
            rows.append({
                "ms": int(m.group("ms")),
                "units": int(m.group("units")),
                "wt": int(m.group("wt")),
                "seed": int(m.group("seed")),
                "gidx": int(m.group("gidx")),
                "arm": j.group("arm") if j else "?",
                "depth": int(j.group("depth")) if j else -1,
                "job": m.group("job"),
            })
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--top", type=int, default=15)
    a = ap.parse_args()

    rows = parse(a.log)
    if not rows:
        print("no SLOW-GAME records found in %s" % a.log)
        return 1

    total_h = sum(r["ms"] for r in rows) / 3_600_000.0
    aband = [r for r in rows if r["wt"] == INT_MIN]
    print("=== SLOW-GAME census: %s ===" % a.log)
    print("records %d   wall %.1f core-hours   abandoned %d (%.1f core-hours, DISCARDED)"
          % (len(rows), total_h, len(aband),
             sum(r["ms"] for r in aband) / 3_600_000.0))

    # A game's IDENTITY is (seed, game-index) -- that is what the repro reproduces, and it is stable
    # across arms and depths, which is exactly the axis the degeneracy shows up on.
    by_game = collections.defaultdict(list)
    for r in rows:
        by_game[(r["seed"], r["gidx"])].append(r)

    print("\n=== worst GAMES by total wall across all cells (top %d) ===" % a.top)
    print("%-28s %8s %7s %9s  %s" % ("repro", "tot(h)", "cells", "min-depth", "depths seen"))
    ranked = sorted(by_game.items(), key=lambda kv: -sum(r["ms"] for r in kv[1]))
    for (seed, gidx), rs in ranked[:a.top]:
        depths = sorted({(r["arm"], r["depth"]) for r in rs})
        tot = sum(r["ms"] for r in rs) / 3_600_000.0
        hs = [d for aarm, d in depths if aarm == "H"]
        print("--seed %-7d --game-index %-4d %7.2f %7d %9s  %s"
              % (seed, gidx, tot, len(rs), min(hs) if hs else "-",
                 " ".join("%s%d" % d for d in depths)))

    # DEGENERACY SIGNATURE 1: the same game is pathological at MANY depths, including shallow ones.
    print("\n=== DEGENERATE: same game slow at >=3 distinct H depths (depth is NOT the cause) ===")
    flagged = []
    for (seed, gidx), rs in ranked:
        hd = sorted({r["depth"] for r in rs if r["arm"] == "H"})
        if len(hd) >= 3:
            flagged.append(((seed, gidx), rs, hd))
    if not flagged:
        print("  (none)")
    for (seed, gidx), rs, hd in flagged[:a.top]:
        worst_shallow = min((r["ms"] for r in rs if r["arm"] == "H" and r["depth"] == hd[0]),
                            default=0)
        print("  --seed %-7d --game-index %-4d  H depths %s   shallowest (H%d) alone: %.2f h"
              % (seed, gidx, hd, hd[0], worst_shallow / 3_600_000.0))

    # DEGENERACY SIGNATURE 2: unit-rate collapse -- the ceiling's currency decoupling from wall time.
    print("\n=== UNIT-RATE COLLAPSE (ceiling is in units; wall is what we pay) ===")
    rates = [(r["units"] / (r["ms"] / 1000.0), r) for r in rows if r["ms"] > 0]
    rates.sort()
    med = rates[len(rates) // 2][0]
    print("  median rate over slow games: %8.0f units/s" % med)
    print("  %-28s %10s %10s %9s" % ("repro", "units/s", "vs median", "wall(h)"))
    for rate, r in rates[:a.top]:
        print("  --seed %-7d --game-index %-4d %10.0f %9.2fx %9.2f  %s%d"
              % (r["seed"], r["gidx"], rate, rate / med if med else 0,
                 r["ms"] / 3_600_000.0, r["arm"], r["depth"]))

    print("\n=== cost by arm/depth ===")
    by_cell = collections.defaultdict(lambda: [0, 0])
    for r in rows:
        k = "%s%d" % (r["arm"], r["depth"])
        by_cell[k][0] += r["ms"]
        by_cell[k][1] += 1
    for k in sorted(by_cell, key=lambda k: -by_cell[k][0]):
        ms, n = by_cell[k]
        print("  %-4s %8.1f core-h  over %4d slow games" % (k, ms / 3_600_000.0, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
