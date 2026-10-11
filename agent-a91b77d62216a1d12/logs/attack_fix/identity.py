#!/usr/bin/env python3
"""identity.py <head_out> <mine_out>: compare the identity jobs (`<deck>_<cell>_x.cN`) -- the head binary
(no flags) vs this build with every new lever off. Prints per-job differences and a per-deck summary."""
import re, sys
from collections import defaultdict


def load(fn):
    d = {}
    for ln in open(fn, errors='replace'):
        m = re.match(r'(\S+_x\.c\d+): played=(\d+) avg=(\S+) digest=(\S+)', ln)
        if m:
            d[m.group(1)] = (m.group(2), m.group(3), m.group(4))
    return d


A, B = load(sys.argv[1]), load(sys.argv[2])
per = defaultdict(lambda: [0, 0])
for k in sorted(set(A) | set(B)):
    deck_cell = k.rsplit('_x.', 1)[0]
    if A.get(k) == B.get(k):
        per[deck_cell][0] += 1
    else:
        per[deck_cell][1] += 1
        print('DIFF %-30s head=%s mine=%s' % (k, A.get(k), B.get(k)))
for dc in sorted(per):
    print('%-20s identical=%d differing=%d' % (dc, per[dc][0], per[dc][1]))
print('TOTAL identical=%d differing=%d' % (sum(v[0] for v in per.values()), sum(v[1] for v in per.values())))
