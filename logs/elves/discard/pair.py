#!/usr/bin/env python3
"""Paired per-game report for the Elves discard A/B: <batch.err> base arm [arms...].
Unwon (wt=-1) scores 9 (paired arms share the penalty). Prints win-turn sums, slower/faster
counts, mean delta, paired t, and lists each moved game (fmt, gi, base wt, arm wt, repro seed)."""
import re, sys, math
from collections import defaultdict
pat = re.compile(r"\[win\] job=(\S+?)\.(\S+) gi=(\d+) wt=(-?\d+)")
res = defaultdict(dict)
for line in open(sys.argv[1], errors="ignore"):
    for m in pat.finditer(line):
        res[m.group(1)][(m.group(2), int(m.group(3)))] = int(m.group(4))
base = sys.argv[2]
arms = sys.argv[3:]
seed = 9600000
sc = lambda w: 9 if w < 0 else w
for arm in arms:
    for fmt in sorted({f for f, _ in res[base]}):
        keys = sorted(k for k in res[base] if k[0] == fmt and k in res[arm])
        d = [sc(res[arm][k]) - sc(res[base][k]) for k in keys]
        n = len(d)
        if n == 0: continue
        mean = sum(d) / n
        var = sum((x - mean) ** 2 for x in d) / (n - 1) if n > 1 else 0
        se = math.sqrt(var / n) if var > 0 else 0
        wins_b = sum(1 for k in keys if res[base][k] >= 0); wins_a = sum(1 for k in keys if res[arm][k] >= 0)
        sum_b = sum(sc(res[base][k]) for k in keys); sum_a = sum(sc(res[arm][k]) for k in keys)
        print(f"{arm:>6} vs {base} {fmt:>4} n={n} won {wins_b}->{wins_a} sum {sum_b}->{sum_a} "
              f"delta={mean:+.5f} se={se:.5f} t={(mean/se if se else 0):+.2f} "
              f"faster={sum(x<0 for x in d)} slower={sum(x>0 for x in d)}")
        for k, x in zip(keys, d):
            if x != 0:
                print(f"    {fmt} gi={k[1]} {base}={res[base][k]} {arm}={res[arm][k]} ({'SLOWER' if x>0 else 'faster'}) repro --seed {seed+k[1]} --game-index {k[1]}")
