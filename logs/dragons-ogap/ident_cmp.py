#!/usr/bin/env python3
# ident_cmp.py <base batch.out> <new batch.out>: per-job digest identity (oracle OFF), grouped by deck/format prefix.
import re, sys
from collections import defaultdict
def load(p):
    d = {}
    for line in open(p):
        m = re.match(r'^(\S+): played=(\d+) avg=(\S+) digest=(\S+)', line)
        if m:
            d[m.group(1)] = (m.group(2), m.group(3), m.group(4))
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
grp = defaultdict(lambda: [0, 0, 0])
for k in sorted(set(a) | set(b)):
    g = grp[k.split('__')[0]]
    g[0] += 1
    if k in a and k in b:
        g[1] += a[k] == b[k]
    else:
        g[2] += 1
bad = 0
for g, (n, same, miss) in sorted(grp.items()):
    print('%-14s jobs=%d identical=%d missing=%d' % (g, n, same, miss))
    bad += n - same
print('IDENTICAL' if bad == 0 else 'DIFFERS (%d jobs)' % bad)
