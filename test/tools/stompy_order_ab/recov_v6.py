#!/usr/bin/env python3
"""Recovery verdicts for the v6 slower games. usage: recov_v6.py <v6_dir> <rec_dir> [arm ...]
<rec_dir>/wins holds <deck>.<cell>.<arm>-s<stage>.g<gi>.wins. A slower game (old -> new) recovers at stage 1
if its s1 win turn <= old, else at stage 2 if s2 <= old. Loss = 9."""
import collections
import glob
import os
import sys

LOSS = 9
d6, rec = sys.argv[1], sys.argv[2]
arms = sys.argv[3:] or ["ofixall", "fixall", "onotopall"]
res = {}
for p in glob.glob(os.path.join(rec, "wins", "*.wins")):
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
for arm in arms:
    p = os.path.join(d6, "slower.%s.txt" % arm)
    if not os.path.exists(p):
        continue
    cnt = collections.Counter()
    per = collections.defaultdict(collections.Counter)
    fails = []
    for row in open(p):
        f = row.split()
        if not f:
            continue
        deck, cell, gi, wo, wn = f[0], f[1], int(f[2]), int(f[3]), int(f[4])
        a = res.get((arm, deck, cell, 1, gi))
        b = res.get((arm, deck, cell, 2, gi))
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
        per[(deck, cell)][v] += 1
    print("== %s: %s" % (arm, dict(cnt)))
    for k in sorted(per):
        print("   %-16s %-5s %s" % (k[0], k[1], dict(per[k])))
    for x in fails:
        print("   NO  %s %s gi%d %d->%d s1=%s s2=%s" % x)
