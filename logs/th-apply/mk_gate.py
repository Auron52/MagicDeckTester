#!/usr/bin/env python3
# mk_gate.py (th-apply, from logs/worldfire-ad/mk_gate.py): the Treasure Hunt slice of mana-clean gate
# claude-queue-20261125a (seed 30000000 + gi, gi from 100000), re-run under MTG_MANA_ORACLE=2 with levers pinned per arm.
# ONE pooled manifest.
#   usage: python3 mk_gate.py <out.json> [--n GAMES] [--start GI] [--arms base|offon] [--chunk N]
import json, argparse
ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--n', type=int, default=6000)
ap.add_argument('--start', type=int, default=100000)
ap.add_argument('--chunk', type=int, default=100)
ap.add_argument('--arms', default='base')
o = ap.parse_args()
L = ['MTG_LINE_KEEP_CRIT_FILTER']
arms = {'base': {}} if o.arms == 'base' else {'off': {k: False for k in L}, 'on': {k: True for k in L}}
D = 'decks/Treasure Hunt/Treasure Hunt.txt'; P = 'decks/Treasure Hunt/Treasure Hunt.profile.json'
jobs = []
for off in range(0, o.n, o.chunk):
    gi = o.start + off
    for arm, fl in arms.items():
        jobs.append({'name': '%s__treasure_hunt__g%d' % (arm, gi), 'deck': D, 'profile': P,
                     'games': min(o.chunk, o.n - off), 'seed': 30000000 + gi, 'game_index': gi, 'flags': dict(fl)})
json.dump({'jobs': jobs}, open(o.out, 'w'), indent=0)
print(len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games')
