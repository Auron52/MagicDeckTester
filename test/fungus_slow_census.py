#!/usr/bin/env python3
"""Read a Fungus cost census log and report the tail, PAIRED by game, in UNITS.

The census is one pooled `mtg --batch` whose jobs come in arms that share a seed block, e.g.
`Fungus~poolOFF~s1600000` / `Fungus~poolON~s1600000`. Every game therefore exists in both arms
with the same shuffle, so cost can be compared game-for-game instead of only in aggregate --
which matters here because the tail is ~2% of games and carries most of the cost.

UNITS, NOT MILLISECONDS, IS THE COMPARISON CURRENCY. A unit is one simulated turn-step
(ai/GameWorkMeter.h): deterministic, identical on every machine and every run for a given
(seed, arm). Wall ms is measured on a box shared with 23 sibling workers and with other tenants,
so two arms that ran at different moments are not comparable in it. Wall is still reported,
because the RATIO ms/unit is itself the measurement this deck is about: the virtual-ms budget
assumes a fixed per-unit cost (SearchBudget::NODES_PER_VIRTUAL_MS = 900 units/ms, i.e. 0.00111
ms/unit), and a deck whose real ratio is orders of magnitude above that has a budget that no
longer means what it says.

Run with `MTG_SLOW_GAME_MS=1` so EVERY game emits a line; the default 30000 reports only the tail.

    [goldfish] SLOW-GAME <ms>ms  job=<job> gi=<n> wt=<t> units=<u>  repro: --seed ...
    [win] job=<job> gi=<n> wt=<t>

usage: fungus_slow_census.py <run.log> [--min-ms N] [--top N] [--baseline census1.out]
"""
import argparse
import re
import sys
from collections import defaultdict

SLOW = re.compile(r"SLOW-GAME (\d+)ms\s+job=(\S+) gi=(\d+) wt=(-?\d+)(?: units=(\d+))?")
WIN = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")

# SearchBudget::NODES_PER_VIRTUAL_MS -- the calibration the whole budget system rests on.
NODES_PER_VIRTUAL_MS = 900.0
CONTRACT_MS_PER_UNIT = 1.0 / NODES_PER_VIRTUAL_MS


def split_job(job):
    """`Fungus~poolOFF~s1600000` -> ('poolOFF', 's1600000').

    The seed block is the LAST field, not the third: census 1's jobs carry an extra tag
    (`Fungus~base~play~s1600000`), and reading it positionally would file every block under
    "play" and silently collapse twelve blocks into one.
    """
    parts = job.split("~")
    if len(parts) < 3:
        return job, ""
    return parts[1], parts[-1]


def load(path):
    ms = defaultdict(dict)     # arm -> (block, gi) -> wall ms
    units = defaultdict(dict)  # arm -> (block, gi) -> work units
    wins = defaultdict(dict)   # arm -> (block, gi) -> win turn
    with open(path, errors="replace") as fh:
        for line in fh:
            m = SLOW.search(line)
            if m:
                arm, block = split_job(m.group(2))
                key = (block, int(m.group(3)))
                ms[arm][key] = int(m.group(1))
                if m.group(5) is not None:
                    units[arm][key] = int(m.group(5))
                continue
            m = WIN.search(line)
            if m:
                arm, block = split_job(m.group(1))
                wins[arm][(block, int(m.group(2)))] = int(m.group(3))
    return ms, units, wins


def pct(vals, p):
    if not vals:
        return 0
    s = sorted(vals)
    return s[min(len(s) - 1, int(len(s) * p))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--min-ms", type=int, default=30000,
                    help="tail threshold for the wall census (default 30000, matching the batch "
                         "runner's own default so the numbers compare with census 1)")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--baseline", help="an earlier census log to compare the tail against")
    args = ap.parse_args()

    ms, units, wins = load(args.log)
    arms = sorted(set(ms) | set(wins))
    if not arms:
        print("no census lines found", file=sys.stderr)
        return 1

    print("=== games seen ===")
    for a in arms:
        print(f"  {a:10s} {len(wins[a]):6d} finished   {len(ms[a]):6d} cost lines   "
              f"{len(units[a]):6d} with units")

    print("\n=== play metric (all finished games; losses score max_turns+1) ===")
    for a in arms:
        v = list(wins[a].values())
        if v:
            print(f"  {a:10s} n={len(v):6d}  avg win turn {sum(v)/len(v):.4f}")

    print(f"\n=== COST IN UNITS (deterministic; the A/B currency) ===")
    for a in arms:
        u = list(units[a].values())
        if not u:
            continue
        print(f"  {a:10s} n={len(u):6d}  total {sum(u):>14,d}  median {pct(u,.5):>9,d}  "
              f"p90 {pct(u,.9):>10,d}  p99 {pct(u,.99):>11,d}  max {max(u):>12,d}")

    print(f"\n=== wall tail at >= {args.min_ms} ms (contention-sensitive; for comparison only) ===")
    for a in arms:
        tail = {k: v for k, v in ms[a].items() if v >= args.min_ms}
        n = len(wins[a]) or 1
        tot = sum(tail.values()) / 3600000.0
        print(f"  {a:10s} n={len(tail):4d} ({100.0*len(tail)/n:4.1f}% of games)  "
              f"median {pct(list(tail.values()),.5)/1000.0:7.1f}s  "
              f"p90 {pct(list(tail.values()),.9)/1000.0:8.1f}s  "
              f"worst {(max(tail.values())/1000.0 if tail else 0):8.1f}s  total {tot:6.2f} h")

    # THE RATIO. What the budget system assumes vs what this deck actually costs per unit.
    print(f"\n=== per-unit wall cost vs the budget's calibration "
          f"({CONTRACT_MS_PER_UNIT:.5f} ms/unit) ===")
    for a in arms:
        common = set(ms[a]) & set(units[a])
        if not common:
            continue
        tm = sum(ms[a][k] for k in common)
        tu = sum(units[a][k] for k in common)
        ratio = (tm / tu) if tu else 0.0
        # Per-game ratio spread says whether the miscalibration is uniform or lives in the tail.
        per = sorted((ms[a][k] / units[a][k]) for k in common if units[a][k] > 0)
        print(f"  {a:10s} aggregate {ratio:.5f} ms/unit = {ratio/CONTRACT_MS_PER_UNIT:6.1f}x the "
              f"contract   per-game median {pct(per,.5)/CONTRACT_MS_PER_UNIT:6.1f}x  "
              f"p99 {pct(per,.99)/CONTRACT_MS_PER_UNIT:7.1f}x")

    if len(arms) == 2:
        a, b = arms
        common = set(wins[a]) & set(wins[b])
        diff = [k for k in common if wins[a][k] != wins[b][k]]
        print(f"\n=== paired over {len(common)} common games ===")
        print(f"  win turn differs on {len(diff)} game(s)"
              + (f": {sorted(diff)[:10]}" if diff else "  -> play is metric-identical"))
        cu = set(units[a]) & set(units[b])
        if cu:
            ua = sum(units[a][k] for k in cu)
            ub = sum(units[b][k] for k in cu)
            worse = [k for k in cu if units[b][k] > units[a][k]]
            better = [k for k in cu if units[b][k] < units[a][k]]
            print(f"  units over {len(cu)} paired games: {a} {ua:,d}   {b} {ub:,d}"
                  + (f"   ({ua/ub:.3f}x)" if ub else ""))
            print(f"  {b} cheaper on {len(better)} games, more expensive on {len(worse)}, "
                  f"equal on {len(cu)-len(better)-len(worse)}")
        print(f"\n  top {args.top} games by cost (units), with the paired twin:")
        rows = sorted(cu, key=lambda k: -max(units[a][k], units[b][k]))
        for k in rows[:args.top]:
            block, gi = k
            seed = int(block[1:]) + gi if block.startswith("s") else gi
            r = units[a][k] / units[b][k] if units[b][k] else 0.0
            print(f"    {block} gi={gi:<4d} seed={seed:<9d} "
                  f"{a}={units[a][k]:>11,d}u/{ms[a][k]/1000.0:8.1f}s  "
                  f"{b}={units[b][k]:>11,d}u/{ms[b][k]/1000.0:8.1f}s  {r:5.2f}x  "
                  f"wt={wins[a].get(k,'?')}")

    if args.baseline:
        bms, bunits, bwins = load(args.baseline)
        print(f"\n=== baseline {args.baseline} ===")
        for a in sorted(set(bms) | set(bwins)):
            tail = {k: v for k, v in bms[a].items() if v >= args.min_ms}
            n = len(bwins[a]) or 1
            v = list(bwins[a].values())
            print(f"  {a:10s} n={len(v):6d} avg {sum(v)/len(v):.4f}   "
                  f"tail n={len(tail):4d} ({100.0*len(tail)/n:4.1f}%)  "
                  f"total {sum(tail.values())/3600000.0:6.2f} h  "
                  f"worst {(max(tail.values())/1000.0 if tail else 0):8.1f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
