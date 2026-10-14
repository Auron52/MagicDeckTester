#!/usr/bin/env python3
"""Paired A/B manifest for the Hinata2 USER cast order (MTG_HINATA_ORDER_FULL).

ONE pooled batch, both arms as per-job heurarm flag overrides on the SAME binary:
  old = MTG_HINATA_ORDER_FULL off (must be byte-identical to the ff3e205b binary -- checked separately)
  new = MTG_HINATA_ORDER_FULL on (+ any extra flags given with --new-flag NAME=0/1)
Cells: shipped (profile's value_play: no depth/budget keys), d3 b10, d0. hinata + hinata2hg.
Held-out seeds from 11100001, each cell its own disjoint range (base + games < next base).
"""
import argparse, json

WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155'
DECK = WT + '/decks/Hinata2/Hinata2.cod'
PROF = WT + '/decks/Hinata2/Hinata2.profile.json'

ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--ship', type=int, default=1000)
ap.add_argument('--d3', type=int, default=2000)
ap.add_argument('--d0', type=int, default=10000)
ap.add_argument('--hg-frac', type=float, default=0.5)
ap.add_argument('--chunk-ship', type=int, default=250)
ap.add_argument('--chunk-d3', type=int, default=500)
ap.add_argument('--chunk-d0', type=int, default=5000)
ap.add_argument('--new-flag', action='append', default=[], help='NAME=0|1 extra flag on the new arm')
ap.add_argument('--arms', default='old,new')
ap.add_argument('--only', default='', help='comma list of cells to keep, e.g. hinata_ship,hinata2hg_d3')
a = ap.parse_args()

extra = {}
for f in a.new_flag:
    k, v = f.split('=')
    extra[k] = v not in ('0', 'false', '')

arms = {
    'old': {'MTG_HINATA_ORDER_FULL': False},
    'new': dict({'MTG_HINATA_ORDER_FULL': True}, **extra),
}

# (cell, 2hg?, depth or None (shipped), budget, games, seed)
cells = []
for hg in (False, True):
    frac = a.hg_frac if hg else 1.0
    base = 11150001 if hg else 11100001
    tag = 'hinata2hg' if hg else 'hinata'
    cells.append((tag + '_ship', hg, None, None, int(a.ship * frac), base, a.chunk_ship))
    cells.append((tag + '_d3', hg, 3, 10, int(a.d3 * frac), base + 10000, a.chunk_d3))
    cells.append((tag + '_d0', hg, 0, 0, int(a.d0 * frac), base + 20000, a.chunk_d0))
# disjointness: every cell's [seed, seed+games) must not overlap another's
spans = sorted((c[5], c[5] + c[4], c[0]) for c in cells)
for (s0, e0, n0), (s1, e1, n1) in zip(spans, spans[1:]):
    assert e0 <= s1, (n0, n1, 'seed ranges overlap')

keep = set(x for x in a.only.split(',') if x)
jobs = []
for (cell, hg, depth, bud, games, seed, chunk) in cells:
    if keep and cell not in keep:
        continue
    for arm in a.arms.split(','):
        off = 0
        while off < games:
            n = min(chunk, games - off)
            j = {'name': '%s_%s_c%05d' % (cell, arm, off), 'deck': DECK, 'profile': PROF,
                 'games': n, 'seed': seed + off, 'game_index': off, 'flags': arms[arm]}
            if hg:
                j['starting_life'] = 30
                j['opponent_heads'] = 2
            if depth is not None:
                j['depth'] = depth
                j['budget_ms'] = bud
                j['ignore_play_profile'] = True
            # LPT: the expensive searched games first, so the cheap ones backfill the tail
            w = 0
            if depth is None:
                w = 9000
            elif depth > 0:
                w = 3000
            if hg and w:
                w += 500
            j['weight'] = w
            jobs.append(j)
            off += n
json.dump({'jobs': jobs}, open(a.out, 'w'), indent=1)
tot = sum(j['games'] for j in jobs)
print('jobs=%d games=%d -> %s' % (len(jobs), tot, a.out))
