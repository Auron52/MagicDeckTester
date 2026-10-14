#!/usr/bin/env python3
"""Paired per-game read of a gen_ab.py batch: faster / slower / net per cell, units ratio, slower list.

usage: analyze_ab.py <game-log-dir> [old_arm new_arm] [--slower-out FILE]
Loss = turn 9 (an unwon game, wt <= 0, scores max_turns + 1 = 9).
"""
import glob, os, re, sys, json
from collections import defaultdict

d = sys.argv[1]
rest = sys.argv[2:]
slower_out = None
if '--slower-out' in rest:
    i = rest.index('--slower-out')
    slower_out = rest[i + 1]
    rest = rest[:i] + rest[i + 2:]
args = [x for x in rest if not x.startswith('--')]
old_arm, new_arm = (args + ['old', 'new'])[:2]

BASES = {'hinata_ship': 11100001, 'hinata_d3': 11110001, 'hinata_d0': 11120001,
         'hinata2hg_ship': 11150001, 'hinata2hg_d3': 11160001, 'hinata2hg_d0': 11170001}

data = defaultdict(dict)   # (cell, arm) -> gi -> [wt, digest, units]
pat = re.compile(r'^(?P<cell>.+?)_(?P<arm>[A-Za-z0-9]+)_c(?P<off>\d+)\.wins$')
for f in glob.glob(os.path.join(d, '*.wins')):
    m = pat.match(os.path.basename(f))
    if not m:
        continue
    key = (m.group('cell'), m.group('arm'))
    for line in open(f):
        p = line.split()
        if len(p) < 2:
            continue
        gi = int(p[0]); wt = int(p[1]); dg = p[2] if len(p) > 2 else ''
        data[key][gi] = [wt, dg, None]
    uf = f[:-5] + '.units'
    if os.path.exists(uf):
        for line in open(uf):
            p = line.split()
            if len(p) >= 2 and int(p[0]) in data[key]:
                data[key][int(p[0])][2] = float(p[1])

def score(wt):
    return wt if wt > 0 else 9

cells = sorted(set(c for (c, a) in data))
rows = []
slower = []
for c in cells:
    o = data.get((c, old_arm), {}); n = data.get((c, new_arm), {})
    gis = sorted(set(o) & set(n))
    if not gis:
        continue
    fa = sl = pc = 0; net = 0; uo = un = 0.0; have_u = True
    so = sn = 0
    for gi in gis:
        a, b = score(o[gi][0]), score(n[gi][0])
        so += a; sn += b
        if b < a: fa += 1
        elif b > a:
            sl += 1
            slower.append((c, BASES.get(c, 0) + gi, gi, o[gi][0], n[gi][0]))
        elif o[gi][1] != n[gi][1]: pc += 1
        net += b - a
        if o[gi][2] is None or n[gi][2] is None: have_u = False
        else: uo += o[gi][2]; un += n[gi][2]
    rows.append((c, len(gis), so / len(gis), sn / len(gis), fa, sl, net, pc,
                 (un / uo) if (have_u and uo > 0) else float('nan'),
                 len(o), len(n)))
print('%-16s %6s %8s %8s %6s %6s %6s %7s %8s' % ('cell', 'n', 'old_avg', 'new_avg', 'faster', 'slower', 'net', 'playchg', 'units'))
for r in rows:
    print('%-16s %6d %8.4f %8.4f %6d %6d %+6d %7d %8.4f' % r[:9] + ('' if r[9] == r[10] == r[1] else '   (old n=%d new n=%d)' % (r[9], r[10])))
print()
print('slower games: %d' % len(slower))
for s in sorted(slower):
    print('  %-16s seed=%d gi=%d  %d -> %d' % s)
if slower_out:
    json.dump(slower, open(slower_out, 'w'))
