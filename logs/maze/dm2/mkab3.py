#!/usr/bin/env python3
"""mkab3.py <overnight manifest> <smoke 2hg manifest> <out>: held-out A/B for DM-2. Drops d0 jobs; adds held-out 2HG
jobs (seeds 4004..7007) from the smoke 2HG job templates; chunks every job into <=25 games (seed = base + off,
game_index = off); three arms innermost, order rotated per chunk:
  base  = the committed tree (MTG_LEAF_GAME_OVER off, MTG_INWIN_EXIT_GUARD off)
  guard = exit guard only
  ship  = both on (the branch)"""
import json, sys
on = json.load(open(sys.argv[1]))['jobs']; sm = json.load(open(sys.argv[2]))['jobs']
jobs = [j for j in on if j.get('depth', 5) != 0]
for t in sm:
    for s in (4004, 5005, 6006, 7007):
        k = dict(t); k['seed'] = s; k['games'] = 150; k['name'] = t['name'].replace('_smoke_', '_heldout_').replace('_s1001', f'_s{s}')
        jobs.append(k)
ARMS = [('base', {'MTG_LEAF_GAME_OVER': False, 'MTG_INWIN_EXIT_GUARD': False}),
        ('guard', {'MTG_LEAF_GAME_OVER': False, 'MTG_INWIN_EXIT_GUARD': True}),
        ('ship', {'MTG_LEAF_GAME_OVER': True, 'MTG_INWIN_EXIT_GUARD': True})]
out = []; rot = 0; C = 25; w = 0
for j in jobs:
    g0 = j.get('game_index', 0)
    for o in range(0, j['games'], C):
        n = min(C, j['games'] - o)
        arms = ARMS[rot % 3:] + ARMS[:rot % 3]; rot += 1
        for a, f in arms:
            k = dict(j); k['name'] = f"{a}__{j['name']}_o{o}"; k['flags'] = dict(f)
            k['seed'] = j['seed'] + o; k['game_index'] = g0 + o; k['games'] = n
            if 'weight' in j: k['weight'] = max(1, j['weight'] * n // j['games'])
            out.append(k)
    w += j.get('weight', 0)
json.dump({'jobs': out}, open(sys.argv[3], 'w'), indent=0)
print(len(jobs), 'base jobs', len(out), 'arm-chunks', sum(j['games'] for j in jobs), 'games/arm', 'weight/arm', w)
