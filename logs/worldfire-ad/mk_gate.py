#!/usr/bin/env python3
# mk_gate.py: the Worldfire slice of mana-clean gate claude-queue-20261125a (seeds 30000000 + gi, gi 0-22999, the gate's
# own chunking), re-run under MTG_MANA_ORACLE=2 with the three line-keep levers pinned per arm. ONE pooled manifest.
#   usage: python3 mk_gate.py <gate manifest.on.0.json> <out.json> [--n GAMES] [--loo]
#   arms: off (all three false), on (all three true); --loo adds leave-one-out arms (one lever false each).
import json, sys, argparse
ap = argparse.ArgumentParser()
ap.add_argument('gate'); ap.add_argument('out')
ap.add_argument('--n', type=int, default=23000)
ap.add_argument('--loo', action='store_true')
ap.add_argument('--loo-n', type=int, default=23000)
o = ap.parse_args()
L = ['MTG_LINE_KEEP_CRIT_MULTI', 'MTG_LINE_KEEP_SUSPEND', 'MTG_LINE_KEEP_RITUAL']
arms = {'off': {k: False for k in L}, 'on': {k: True for k in L}}
if o.loo:
    for k in L:
        a = {x: True for x in L}; a[k] = False
        arms['no' + k.split('_')[-1].lower()] = a
src = [j for j in json.load(open(o.gate))['jobs'] if j['name'].startswith('worldfire__')]
jobs = []
for j in src:
    if j['game_index'] >= o.n:
        continue
    for arm, fl in arms.items():
        if arm not in ('off', 'on') and j['game_index'] >= o.loo_n:
            continue
        k = dict(j); k['name'] = '%s__%s' % (arm, j['name']); k['flags'] = dict(fl)
        jobs.append(k)
json.dump({'jobs': jobs}, open(o.out, 'w'), indent=0)
print(len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games')
