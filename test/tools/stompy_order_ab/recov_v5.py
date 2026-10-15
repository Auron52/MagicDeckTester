#!/usr/bin/env python3
"""Recovery verdicts for the v5 batch. usage: recov_v5.py <v4_dir> <v5_dir>
Job names: <deck>.<cell>.<arm>-s<stage>.g<gi>. A slower game (old -> new) recovers at stage 1 if its s1
win turn <= old, else at stage 2 if s2 <= old. Loss = 9."""
import collections
import glob
import os
import sys

LOSS = 9
v4, v5 = sys.argv[1], sys.argv[2]
res = {}
for p in glob.glob(os.path.join(v5, "wins", "*.wins")):
    parts = os.path.basename(p)[:-5].split(".")
    if len(parts) != 4 or "-s" not in parts[2]:
        continue
    deck, cell, armst, _ = parts
    arm, st = armst.split("-s")
    for line in open(p):
        f = line.split()
        if len(f) >= 2:
            wt = int(f[1])
            res[(arm, deck, cell, int(st), int(f[0]))] = LOSS if wt < 0 else wt
for arm in ("ofix", "onotop", "fix"):
    cnt = collections.Counter()
    fails = []
    per_cell = collections.defaultdict(collections.Counter)
    for row in open(os.path.join(v4, "slower.%s.txt" % arm)):
        f = row.split()
        if not f:
            continue
        deck, cell, gi, wo, wn = f[0], f[1], int(f[2]), int(f[3]), int(f[4])
        a = res.get((arm, deck, cell, 1, gi)); b = res.get((arm, deck, cell, 2, gi))
        if a is not None and a <= wo:
            v = "stage1"
        elif b is not None and b <= wo:
            v = "stage2"
        elif a is None or b is None:
            v = "pending"
        else:
            v = "NO"
            fails.append((deck, cell, gi, wo, wn, a, b))
        cnt[v] += 1
        per_cell[(deck, cell)][v] += 1
    print("== %s: %s" % (arm, dict(cnt)))
    for k in sorted(per_cell):
        print("   %-10s %-5s %s" % (k[0], k[1], dict(per_cell[k])))
    for x in fails:
        print("   NO  %s %s gi%d %d->%d s1=%s s2=%s" % x)
