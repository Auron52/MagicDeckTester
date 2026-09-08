#!/usr/bin/env python3
"""Paired per-game comparison of two batch .wins files (same seed base, same game indices).
usage: paired_wins.py <base.wins> <arm.wins>   (columns: gi win_turn [digest]; win_turn<=0 = no win)"""
import sys, math

def load(p, max_turns=8):
    d = {}
    for line in open(p):
        parts = line.split()
        if len(parts) < 2: continue
        gi, wt = int(parts[0]), int(parts[1])
        d[gi] = wt if wt > 0 else max_turns + 1
    return d

a, b = load(sys.argv[1]), load(sys.argv[2])
common = sorted(set(a) & set(b))
diffs = [b[g] - a[g] for g in common]
n = len(diffs)
mean = sum(diffs) / n
var = sum((x - mean) ** 2 for x in diffs) / (n - 1) if n > 1 else 0.0
se = math.sqrt(var / n) if n > 1 else 0.0
t = mean / se if se > 0 else float('inf')
better = sum(1 for x in diffs if x < 0); worse = sum(1 for x in diffs if x > 0)
print(f"n={n} base_avg={sum(a[g] for g in common)/n:.4f} arm_avg={sum(b[g] for g in common)/n:.4f} "
      f"delta={mean:+.4f} se={se:.4f} t={t:+.2f} 95%CI=[{mean-1.96*se:+.4f},{mean+1.96*se:+.4f}] "
      f"better={better} worse={worse} same={n-better-worse}")
