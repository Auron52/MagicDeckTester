#!/usr/bin/env python3
"""cmp3.py <ab dir> : per deck, per arm pair (base->guard, base->ship): net turns, better/worse games, units ratio;
and the list of ship-slower games (job, gi, seed, base, ship)."""
import os, re, sys
from collections import defaultdict
D = sys.argv[1]; W = os.path.join(D, 'wins')
g = defaultdict(dict)   # (job, gi) -> arm -> turn
for f in os.listdir(W):
    if not f.endswith('.wins'): continue
    arm, job = f[:-5].split('__', 1)
    for ln in open(os.path.join(W, f)):
        p = ln.split()
        if len(p) >= 2: g[(job, int(p[0]))][arm] = int(p[1])
U = defaultdict(lambda: defaultdict(int)); seeds = {}
for ln in open(os.path.join(D, 'out')):
    m = re.match(r'(\w+?)__(\S+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=(\d+) units=(\d+)', ln)
    if m:
        deck = re.sub(r'_(overnight|heldout\d*)_.*', '', m[2]); U[deck][m[1]] += int(m[7])
def deck(job): return re.sub(r'_(overnight|heldout\d*)_.*', '', job)
S = defaultdict(lambda: defaultdict(lambda: [0, 0, 0, 0]))
slow = []
for (job, gi), a in g.items():
    if 'base' not in a: continue
    for arm in ('guard', 'ship'):
        if arm not in a: continue
        d = a[arm] - a['base']; s = S[deck(job)][arm]
        s[0] += d; s[1] += d < 0; s[2] += d > 0; s[3] += 1
        if arm == 'ship' and d > 0: slow.append((job, gi, a['base'], a['ship'], a.get('guard')))
for dk in sorted(S):
    line = f"{dk:20}"
    for arm in ('guard', 'ship'):
        s = S[dk][arm]; ur = U[dk][arm] / max(1, U[dk]['base'])
        line += f" | {arm}: n={s[3]} net={s[0]:+d} better={s[1]} worse={s[2]} units={ur:.3f}x"
    print(line)
print(f"\nship-slower games: {len(slow)}")
for x in sorted(slow): print('  ', *x)
