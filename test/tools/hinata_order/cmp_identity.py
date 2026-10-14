#!/usr/bin/env python3
"""cmp_identity.py BASE_DIR NEW_DIR -- per-job, per-game byte identity of two batch runs (.wins: gi wt digest)."""
import glob, os, sys
a, b = sys.argv[1], sys.argv[2]
def load(d):
    out = {}
    for f in glob.glob(os.path.join(d, 'wins', '*.wins')):
        out[os.path.basename(f)[:-5]] = sorted(l.split() for l in open(f) if l.strip())
    return out
A, B = load(a), load(b)
same = diff = 0
games = 0
bad = []
for k in sorted(set(A) | set(B)):
    if k not in A or k not in B:
        bad.append((k, 'missing in ' + ('base' if k not in A else 'new')))
        continue
    games += len(A[k])
    if A[k] == B[k]:
        same += 1
    else:
        diff += 1
        nd = sum(1 for x, y in zip(A[k], B[k]) if x != y) + abs(len(A[k]) - len(B[k]))
        bad.append((k, '%d of %d games differ' % (nd, len(A[k]))))
print('jobs identical: %d   jobs differing: %d   games compared: %d' % (same, diff, games))
for k, why in bad:
    print('  DIFF', k, why)
