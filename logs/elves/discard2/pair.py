#!/usr/bin/env python3
"""Paired per-game report: pair.py <batch.err> <base_seed>. Arms on vs off, per format.
Unwon (wt=-1) scores 9 in both arms' sums. Prints win-turn sums, mean delta (on - off), se, t,
slower/faster counts and each moved game with its repro seed."""
import re, sys, math
from collections import defaultdict
pat = re.compile(r"\[win\] job=(on|off)\.(std|2hg)\.c\d+ gi=(\d+) wt=(-?\d+)")
res = defaultdict(dict)
for line in open(sys.argv[1], errors="ignore"):
    for m in pat.finditer(line):
        res[m.group(1)][(m.group(2), int(m.group(3)))] = int(m.group(4))
seed = int(sys.argv[2])
sc = lambda w: 9 if w < 0 else w
for fmt in ("std", "2hg"):
    keys = sorted(k for k in res["off"] if k[0] == fmt and k in res["on"])
    d = [sc(res["on"][k]) - sc(res["off"][k]) for k in keys]
    n = len(d)
    if n == 0: print(fmt, "no games"); continue
    mean = sum(d) / n
    var = sum((x - mean) ** 2 for x in d) / (n - 1) if n > 1 else 0
    se = math.sqrt(var / n) if var > 0 else 0
    won_off = sum(res["off"][k] >= 0 for k in keys); won_on = sum(res["on"][k] >= 0 for k in keys)
    print(f"{fmt:>4} n={n} won off={won_off} on={won_on} sum off={sum(sc(res['off'][k]) for k in keys)} "
          f"on={sum(sc(res['on'][k]) for k in keys)} delta(on-off)={mean:+.5f} se={se:.5f} "
          f"t={(mean/se if se else 0):+.2f} faster={sum(x<0 for x in d)} slower={sum(x>0 for x in d)}")
    for k, x in zip(keys, d):
        if x != 0:
            print(f"    {fmt} gi={k[1]} off={res['off'][k]} on={res['on'][k]} ({'SLOWER' if x>0 else 'faster'}) "
                  f"repro --seed {seed+k[1]} --game-index {k[1]}")
