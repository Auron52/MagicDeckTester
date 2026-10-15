#!/usr/bin/env python3
"""Recovery manifest for the A/B's slower games (the NEW arm only).

gen_recovery.py SLOWER.json --out M.json --stage 1|2 [--flag NAME=0/1 ...]
  stage 1: --depth W --budget-ms 100, W = max(shipped depth 5, old win turn)
  stage 2: --depth 8 --budget-ms 0 (unlimited)
SLOWER.json: analyze_ab.py --slower-out rows (cell, seed, gi, old_wt, new_wt). The job seed is base+gi with
game_index gi, which reproduces the batch game exactly.
"""
import argparse, json

WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155'
DECK = WT + '/decks/Hinata2/Hinata2.cod'
PROF = WT + '/decks/Hinata2/Hinata2.profile.json'
ap = argparse.ArgumentParser()
ap.add_argument('slower')
ap.add_argument('--out', required=True)
ap.add_argument('--stage', type=int, required=True)
ap.add_argument('--flag', action='append', default=[])
ap.add_argument('--ship-depth', type=int, default=5)
ap.add_argument('--prefix', default='r', help='job-name prefix (one per arm, so arms can share a batch)')
ap.add_argument('--min-gi', default='', help='CELL=N,... keep only games with gi >= N in those cells')
ap.add_argument('--deck-dir', default=None)
a = ap.parse_args()
flags = {'MTG_HINATA_ORDER_FULL': True}
for f in a.flag:
    k, v = f.split('=')
    flags[k] = v not in ('0', 'false')
if a.deck_dir:
    DECK = a.deck_dir + '/Hinata2.cod'
    PROF = a.deck_dir + '/Hinata2.profile.json'
min_gi = {}
for spec in [x for x in a.min_gi.split(',') if x]:
    c, n = spec.split('=')
    min_gi[c] = int(n)
rows = json.load(open(a.slower))
jobs = []
for (cell, seed, gi, old_wt, new_wt) in rows:
    if cell in min_gi and gi < min_gi[cell]:
        continue
    hg = cell.startswith('hinata2hg')
    if a.stage == 1:
        w = max(a.ship_depth, old_wt if old_wt > 0 else 9)
        depth, bud = w, 100
    else:
        depth, bud = 8, 0
    j = {'name': '%s%d_%s_gi%d_o%d' % (a.prefix, a.stage, cell, gi, old_wt), 'deck': DECK, 'profile': PROF, 'games': 1,
         'seed': seed, 'game_index': gi, 'depth': depth, 'budget_ms': bud, 'ignore_play_profile': True,
         'flags': flags, 'weight': 100 if a.stage == 2 else 10}
    if hg:
        j['starting_life'] = 30
        j['opponent_heads'] = 2
    jobs.append(j)
json.dump({'jobs': jobs}, open(a.out, 'w'), indent=1)
print('jobs=%d -> %s' % (len(jobs), a.out))
