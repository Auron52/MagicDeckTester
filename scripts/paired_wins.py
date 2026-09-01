#!/usr/bin/env python3
"""Paired per-game comparison of batch arms from MTG_DUMP_WINS output.

Usage: paired_wins.py <batch.err> <baseline-arm> [arm ...]

Reads `[win] job=<arm>.<block> gi=<i> wt=<t>` lines. Unwon (wt=-1) scores as 9 --
THE metric is mean turn-to-win with unwon = max_turns+1. Every arm is compared to
the baseline GAME BY GAME on the same seed block, which is what makes the paired
t meaningful: the engine is deterministic, so an unchanged game contributes an
exact zero rather than noise.
"""
import re, sys, math
from collections import defaultdict

pat = re.compile(r"^\[win\] job=(\S+)\.(\S+) gi=(\d+) wt=(-?\d+)")
res = defaultdict(dict)          # arm -> (block, gi) -> score
for line in open(sys.argv[1], errors="ignore"):
    m = pat.match(line)
    if not m:
        continue
    arm, blk, gi, wt = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
    res[arm][(blk, gi)] = 9 if wt < 0 else wt

base = sys.argv[2]
arms = sys.argv[3:] or [a for a in res if a != base]
blocks = sorted({b for b, _ in res[base]})

print(f"{'arm':<12} {'block':<6} {'n':>5} {'mean':>8} {'d vs '+base:>12} {'t':>7} "
      f"{'better':>7} {'worse':>6}")
for arm in [base] + arms:
    for blk in blocks:
        keys = sorted(k for k in res[arm] if k[0] == blk and k in res[base])
        if not keys:
            continue
        mine = [res[arm][k] for k in keys]
        mean = sum(mine) / len(mine)
        if arm == base:
            print(f"{arm:<12} {blk:<6} {len(keys):>5} {mean:>8.4f} {'--':>12} {'':>7} {'':>7} {'':>6}")
            continue
        diffs = [res[arm][k] - res[base][k] for k in keys]
        d = sum(diffs) / len(diffs)
        var = sum((x - d) ** 2 for x in diffs) / (len(diffs) - 1) if len(diffs) > 1 else 0.0
        se = math.sqrt(var / len(diffs)) if var > 0 else 0.0
        t = d / se if se > 0 else 0.0
        better = sum(1 for x in diffs if x < 0)
        worse = sum(1 for x in diffs if x > 0)
        print(f"{arm:<12} {blk:<6} {len(keys):>5} {mean:>8.4f} {d:>+12.4f} {t:>7.2f} "
              f"{better:>7} {worse:>6}")
