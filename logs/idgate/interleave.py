#!/usr/bin/env python3
"""interleave.py <ab manifest> <sr ab manifest> <sr out> <out manifest> [--skip-done wins_dir]: rewrite `weight` (a NON-play key,
so --resume partials stay valid) so decks are dispatched in PROPORTION to their estimated cost, instead of one deck's chunks
all at once (the batch groups a weight tier by profile: 32 merfolkturns d5 games in flight blew the RSS cap twice).
Arm pairs keep adjacent, equal weights (their alternated order is preserved by the job-index tiebreak)."""
import json, re, sys, os, collections
ab, srm, sro, dst = sys.argv[1:5]
skip = sys.argv[sys.argv.index('--skip-done') + 1] if '--skip-done' in sys.argv else None
M = {x['name']: x for x in json.load(open(srm))['jobs']}
rate = collections.defaultdict(list)
for l in open(sro):
    m = re.match(r'off__(\S+): played=(\d+) avg=\S+ digest=\w+ ms=(\d+)', l)
    if not m: continue
    x = M['off__' + m[1]]; k = re.sub(r'_(smoke|regression)_.*', '', m[1])
    rate[(k, x.get('depth'), x.get('budget_ms'))].append((int(m[3]), int(m[2])))
def key(n): return re.sub(r'_(overnight|heldout)_.*', '', n.split('__', 1)[1])
jobs = json.load(open(ab))['jobs']
pairs = collections.OrderedDict()
for x in jobs: pairs.setdefault(x['name'].split('__', 1)[1], []).append(x)
deck = collections.defaultdict(list)
for n, ps in pairs.items():
    x = ps[0]; r = rate.get((key(x['name']), x.get('depth'), x.get('budget_ms'))) or [(1000, 1)]
    deck[key(x['name'])].append((sum(a for a, b in r) / sum(b for a, b in r) * x['games'], n))
tot = {k: sum(c for c, _ in v) for k, v in deck.items()}; done = {k: 0.0 for k in deck}; idx = {k: 0 for k in deck}
order = []
while any(idx[k] < len(deck[k]) for k in deck):
    k = min((k for k in deck if idx[k] < len(deck[k])), key=lambda k: (done[k] / tot[k], k))
    c, n = deck[k][idx[k]]; idx[k] += 1; done[k] += c; order.append(n)
N = len(order); out = []
for i, n in enumerate(order):
    for x in pairs[n]:
        if skip and os.path.exists(f"{skip}/{x['name']}.wins"): continue
        y = dict(x); y['weight'] = N - i; out.append(y)
json.dump({'jobs': out}, open(dst, 'w'), indent=0)
print(len(jobs), '->', len(out), 'jobs;', N, 'pairs interleaved over', len(deck), 'decks')
