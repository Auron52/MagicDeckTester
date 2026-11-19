#!/usr/bin/env python3
"""Aggregate [idg] start-gate census lines (MTG_IDG_CENSUS) from batch stderr files.
usage: idg_census.py <err files...> [--by deck|job]"""
import sys, re, math, statistics as st
from collections import defaultdict
by = 'job'
files = []
for a in sys.argv[1:]:
    if a.startswith('--by='): by = a[5:]
    else: files.append(a)
kv = re.compile(r'(\w+)=(\S+)')
rows = []
for f in files:
    for ln in open(f, errors='replace'):
        if not ln.startswith('[idg] '): continue
        d = dict(kv.findall(ln))
        d['T'] = int(re.search(r' T(\d+) ', ln).group(1))
        rows.append(d)
def key(d):
    j = d['j']
    if by == 'deck': return re.sub(r'_(smoke|regression|overnight).*', '', j)
    if by == 'cell': return re.sub(r'^[a-z0-9]+?(2hg)?_(smoke|regression|overnight)_', r'\2_', j).rsplit('_s', 1)[0]
    return j.rsplit('_s', 1)[0] if by == 'jobd' else j
g = defaultdict(list)
for d in rows: g[key(d)].append(d)
def q(xs, p):
    if not xs: return float('nan')
    xs = sorted(xs); i = min(len(xs) - 1, max(0, int(round(p * (len(xs) - 1))))); return xs[i]
print(f"{'group':34} {'n':>5} {'boot':>5} {'ref':>5} {'refB':>5} {'remR%':>6} | {'adm r med':>9} {'p90':>6} | {'B act/c1 med':>12} {'p90':>6} | {'shFit':>5} {'shBet':>5} {'shU':>8} {'wasteU':>8}")
tot = defaultdict(float)
for k in sorted(g):
    R = g[k]; n = len(R)
    boot = sum(int(d['b']) for d in R)
    ref = [d for d in R if d['fit'] == '0']
    refB = sum(1 for d in ref if d['b'] == '1')
    remr = [float(d['rem']) / max(1, int(d['lim'])) for d in ref if int(d['lim']) > 0]
    adm = [d for d in R if d['fit'] == '1' and int(d['act']) >= 0 and d['st'] == '0']
    ratio = [int(d['act']) / max(1.0, float(d['est'])) for d in adm]
    bact = [int(d['act']) / max(1, int(d['c1'])) for d in adm if d['b'] == '1']
    sh = [d for d in ref if int(d['sh']) >= 0]
    shfit = [d for d in sh if d['sht'] == '0']
    shbet = [d for d in shfit if int(d['shw']) < int(d['pw'])]
    shu = sum(int(d['sh']) for d in shfit); waste = sum(int(d['sh']) for d in sh if d['sht'] == '1')
    for a, b in (('n', n), ('boot', boot), ('ref', len(ref)), ('refB', refB), ('sh', len(sh)), ('shfit', len(shfit)), ('shbet', len(shbet)), ('shu', shu), ('waste', waste)): tot[a] += b
    print(f"{k[:34]:34} {n:5} {boot:5} {len(ref):5} {refB:5} {100*st.mean(remr) if remr else float('nan'):6.1f} | {q(ratio,.5):9.3f} {q(ratio,.9):6.2f} | {q(bact,.5):12.3f} {q(bact,.9):6.2f} | {len(shfit):5} {len(shbet):5} {shu:8} {waste:8}")
print('TOTAL', dict(tot))
