#!/usr/bin/env python3
"""Single-game probes as one tiny pooled manifest.

gen_probe.py --out M.json --game SEED:GI[:2hg] [--game ...] --arm NAME=FLAG=0/1,FLAG=0/1 [--arm ...]
             [--cells ship,d3,d8b0,w5b100]
cells: ship (profile play settings), d3 (d3 b10), d0, dNbM (depth N budget M, e.g. d8b0, d5b100).
"""
import argparse, json, re

WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155'
DECK = WT + '/decks/Hinata2/Hinata2.cod'
PROF = WT + '/decks/Hinata2/Hinata2.profile.json'
ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--game', action='append', required=True)
ap.add_argument('--arm', action='append', required=True)
ap.add_argument('--cells', default='ship,d3')
a = ap.parse_args()

arms = {}
for s in a.arm:
    name, _, fl = s.partition('=')
    flags = {}
    for f in [x for x in fl.split(',') if x]:
        k, v = f.split('=')
        flags[k] = v not in ('0', 'false')
    arms[name] = flags

jobs = []
for g in a.game:
    p = g.split(':')
    seed, gi = int(p[0]), int(p[1])
    hg = len(p) > 2 and p[2] == '2hg'
    for cell in a.cells.split(','):
        for an, fl in arms.items():
            j = {'name': 'p_%s%s_gi%d_%s_%s' % ('hg' if hg else '', seed, gi, cell, an), 'deck': DECK,
                 'profile': PROF, 'games': 1, 'seed': seed, 'game_index': gi, 'weight': 1}
            if fl:
                j['flags'] = fl
            if hg:
                j['starting_life'] = 30
                j['opponent_heads'] = 2
            if cell == 'ship':
                pass
            elif cell == 'd3':
                j.update({'depth': 3, 'budget_ms': 10, 'ignore_play_profile': True})
            elif cell == 'd0':
                j.update({'depth': 0, 'budget_ms': 0, 'ignore_play_profile': True})
            else:
                m = re.match(r'^d(\d+)b(\d+)$', cell)
                assert m, cell
                j.update({'depth': int(m.group(1)), 'budget_ms': int(m.group(2)), 'ignore_play_profile': True})
            jobs.append(j)
json.dump({'jobs': jobs}, open(a.out, 'w'), indent=1)
print('jobs=%d -> %s' % (len(jobs), a.out))
