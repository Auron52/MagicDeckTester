#!/usr/bin/env python3
"""cmp_arms.py <batch out> [gt file] : per job off vs on (avg, digest, units), and off vs GT."""
import re, sys
from collections import OrderedDict
out = sys.argv[1]; gt = {}
if len(sys.argv) > 2:
    for ln in open(sys.argv[2]):
        if '=' in ln and not ln.startswith('#'):
            k, v = ln.strip().split('=', 1); gt[k] = v
R = {}
for ln in open(out):
    m = re.match(r'(\w+?)__(\S+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=(\d+) units=(\d+) cpu=(\d+)', ln)
    if m: R[(m[1], m[2])] = dict(n=int(m[3]), avg=float(m[4]), dg=m[5], ms=int(m[6]), u=int(m[7]), cpu=int(m[8]))
names = sorted({k[1] for k in R})
gtbad = 0; moved = []
tu = {'off': 0, 'on': 0}
for n in names:
    a, b = R.get(('off', n)), R.get(('on', n))
    if a and gt:
        g = gt.get(n)
        if g and g != f"{a['avg']:.4f}/{a['dg']}": gtbad += 1; print('OFF!=GT', n, g, f"{a['avg']:.4f}/{a['dg']}")
    if a and b:
        tu['off'] += a['u']; tu['on'] += b['u']
        if a['dg'] != b['dg']: moved.append((n, a, b))
print(f"jobs={len(names)} off-vs-GT mismatches={gtbad} moved={len(moved)}  units off={tu['off']} on={tu['on']} ratio={tu['on']/max(1,tu['off']):.4f}")
for n, a, b in moved:
    print(f"  {n:38} n={a['n']:4} avg {a['avg']:.4f} -> {b['avg']:.4f} ({(b['avg']-a['avg'])*a['n']:+.0f} turns)  units {a['u']} -> {b['u']} ({b['u']/max(1,a['u']):.3f}x)")
