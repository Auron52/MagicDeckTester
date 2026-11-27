#!/usr/bin/env python3
# mk_gate2.py: the Dragonstorm slice of mana-clean gate claude-queue-20261125a (seed 30000000 + gi, gi 87000-158999, the
# gate's own chunking, taken from a previous slice manifest), re-run under MTG_MANA_ORACLE=2 with the ds-apply2 levers
# pinned per arm. ONE pooled manifest.
#   usage: python3 mk_gate2.py <slice manifest (e.g. ds-apply/gate3.json)> <out.json> [--loo-n GAMES]
#   arms: off / on (both levers); leave-one-out arms (one lever off) on the first --loo-n games of the slice.
import json, argparse
ap = argparse.ArgumentParser()
ap.add_argument('gate'); ap.add_argument('out')
ap.add_argument('--loo-n', type=int, default=18000)
o = ap.parse_args()
L = ['MTG_ONDEMAND_LUMP_KEEP_OWED']
arms = {'off': {k: False for k in L}, 'on': {k: True for k in L}}
for k in L:
    a = {x: True for x in L}; a[k] = False
    arms['no_' + k[4:].lower()] = a
src = []
seen = set()
for j in json.load(open(o.gate))['jobs']:
    key = j['name'].split('__', 1)[1]
    if key in seen: continue
    seen.add(key); src.append(j)
g0 = min(j['game_index'] for j in src)
jobs = []
for j in src:
    for arm, fl in arms.items():
        if arm not in ('off', 'on') and j['game_index'] - g0 >= o.loo_n:
            continue
        k = dict(j); k['name'] = '%s__%s' % (arm, j['name'].split('__', 1)[1]); k['flags'] = dict(fl)
        jobs.append(k)
json.dump({'jobs': jobs}, open(o.out, 'w'), indent=0)
print(len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games')
