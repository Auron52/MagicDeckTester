#!/usr/bin/env python3
"""cmp_arm_identity.py DIR_A ARM_A DIR_B ARM_B -- per-game identity (gi, win turn, digest) of two arms' chunks
that share seeds (same cell, same chunk offsets), e.g. a rebuilt binary's re-run of an arm."""
import glob, os, re, sys
from collections import defaultdict
da, aa, db, ab = sys.argv[1:5]
pat = re.compile(r'^(?P<cell>.+?)_(?P<arm>[A-Za-z0-9]+)_c(?P<off>\d+)\.wins$')
def load(d, arm):
    out = defaultdict(dict)
    for f in glob.glob(os.path.join(d, '*.wins')):
        m = pat.match(os.path.basename(f))
        if not m or m.group('arm') != arm:
            continue
        for line in open(f):
            p = line.split()
            if len(p) >= 2:
                out[m.group('cell')][int(p[0])] = tuple(p[1:])
    return out
A, B = load(da, aa), load(db, ab)
for cell in sorted(set(A) & set(B)):
    gis = sorted(set(A[cell]) & set(B[cell]))
    diff = [g for g in gis if A[cell][g] != B[cell][g]]
    print('%-16s compared=%d differ=%d' % (cell, len(gis), len(diff)))
