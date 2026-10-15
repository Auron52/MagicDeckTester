#!/usr/bin/env python3
"""Recovery verdicts. usage: recov_eval.py <slower.txt> <recovery_wins_dir>
A slower game (old -> new) recovers at stage 1 if its s1 win turn <= old, else at stage 2 if s2 <= old.
Loss = 9."""
import glob
import os
import sys

LOSS = 9
res = {}
for p in glob.glob(os.path.join(sys.argv[2], "*.wins")):
    deck, cell, st, g = os.path.basename(p)[:-5].split(".")
    for line in open(p):
        f = line.split()
        if len(f) >= 2:
            wt = int(f[1])
            res[(deck, cell, st, int(f[0]))] = LOSS if wt < 0 else wt
n = s1 = s2 = 0
fails = []
for row in open(sys.argv[1]):
    f = row.split()
    if not f:
        continue
    deck, cell, gi, wo, wn = f[0], f[1], int(f[2]), int(f[3]), int(f[4])
    a, b = res.get((deck, cell, "s1", gi)), res.get((deck, cell, "s2", gi))
    n += 1
    if a is not None and a <= wo:
        s1 += 1; v = "stage1"
    elif b is not None and b <= wo:
        s2 += 1; v = "stage2"
    else:
        v = "NO"; fails.append((deck, cell, gi, wo, wn, a, b))
    print("%-10s %-5s gi%-6d %d->%d  s1=%s s2=%s  %s" % (deck, cell, gi, wo, wn, a, b, v))
print("\n%d slower: %d recover at stage 1, %d at stage 2, %d do not" % (n, s1, s2, len(fails)))
