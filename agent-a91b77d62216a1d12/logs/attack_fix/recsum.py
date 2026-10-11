#!/usr/bin/env python3
"""recsum.py <recdir>: per game -- old, ab3-new, on, off; the levers whose removal changes `on`
(noX != on) and the levers that alone reach old-or-better from the all-off arm (onlyX <= old)."""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recover import games
d = sys.argv[1]
wt = {}
for ln in open(os.path.join(d, 'err'), errors='replace'):
    m = re.search(r'\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)', ln)
    if m:
        w = int(m.group(3))
        wt[m.group(1)] = w if w > 0 else 9
arms = [a for a, _ in json.load(open(os.path.join(d, 'arms.json')))]
nos = [a for a in arms if a.startswith('no')]
onlys = [a for a in arms if a.startswith('only')]
print('%-10s %-4s %5s %-7s old new  on off   needs(noX!=on)                 alone-recovers(onlyX<=old)' % (
    'deck', 'cell', 'gi', 'src'))
tot = {'on': 0, 'off': 0, 'old': 0, 'new': 0}
rec = 0
n = 0
for (deck, cell, gi, seed, old, new, src) in games():
    k = lambda a: '%s_%s_g%d_%s' % (deck, cell, gi, a)
    on, off = wt.get(k('on')), wt.get(k('off'))
    if on is None:
        continue
    n += 1
    tot['on'] += on; tot['off'] += off; tot['old'] += int(old); tot['new'] += int(new)
    if on <= int(old):
        rec += 1
    needs = ['%s=%d' % (a[2:], wt[k(a)]) for a in nos if wt.get(k(a)) is not None and wt[k(a)] != on]
    alone = [a[4:] for a in onlys if wt.get(k(a)) is not None and wt[k(a)] <= int(old)]
    print('%-10s %-4s %5d %-7s %3s %3s %3d %3d   %-30s %s' % (deck, cell, gi, src, old, new, on, off,
          ' '.join(needs), ' '.join(alone)))
print('games=%d  sum old=%d ab3new=%d on=%d off=%d   on reaches old-or-better: %d/%d' % (
    n, tot['old'], tot['new'], tot['on'], tot['off'], rec, n))
