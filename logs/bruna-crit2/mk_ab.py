#!/usr/bin/env python3
# mk_ab.py (2026-11-24): held-out paired A/B of MTG_LINE_KEEP_CRIT (off vs on), ONE pooled `mtg --batch` manifest.
#
# usage (cwd = the bruna-crit2 worktree):  python3 logs/bruna-crit2/mk_ab.py <out.json> [--base N] [--estimate]
#
# Paired: game gi of a shape -> seed BASE + gi, game_index gi, on both arms (arms interleaved per chunk, so a pair
# finishes together). BASE defaults to 2630000: never used before (the 7a / 7b A/Bs used 1810000, 1830000, 1850000;
# grep docs/ test/ scripts/ build/scratch/*/*.json finds no 263xxxx seed).
#
# ORDER = priority (the USER: "Play settings are the most important"):
#   1. PLAY-SETTING rows: depth AND budget OMITTED, so each deck's enabled value_play block picks both (the [play]
#      line of each job says which); a deck with no enabled block gets the built-in d5 b20. >= 2,000 games per arm.
#   2. d3 b10 and d5 b20, PINNED (ignore_play_profile), the regression tier's searched shapes. 1,000 per arm.
#   3. d0 b0 last and small (500 per arm): "d0 is not very important at all".
# Both arms pin the lever explicitly (flags MTG_LINE_KEEP_CRIT false / true), so the default does not matter.
import json, os, re, sys

N_PLAY, N_SEARCH, N_D0, CHUNK = 2000, 1000, 500, 25
DECKS = ['bruna', 'cyclone', 'niv', 'dragonstorm', 'jumbo', 'selesnyav2', 'stompy', 'elementals', 'spirits',
         'mirrorwing', 'worldfire']
TWO_HG = ['niv', 'selesnyav2']   # 2HG play rows (30 team life, two opponent heads)
HG = {"starting_life": 30, "opponent_heads": 2}
ARMS = {'off': {'MTG_LINE_KEEP_CRIT': False}, 'on': {'MTG_LINE_KEEP_CRIT': True}}

import argparse
ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--base', type=int, default=2630000)
ap.add_argument('--estimate', action='store_true')
opt = ap.parse_args()
out, base = opt.out, opt.base

src = open('test/regression_cases.sh').read()
def amap(name):
    m = re.search(r'^declare -A %s=\((.*?)^\)' % name, src, re.S | re.M)
    o = {}
    for line in m.group(1).splitlines():
        mm = re.match(r'\s+\[(\w+)\]=(".*?"|\S+)', line)
        if mm:
            o[mm.group(1)] = mm.group(2).strip('"')
    return o
F, P = amap('DECK_FILE'), amap('DECK_PROF')
for k in DECKS:
    assert k in F and os.path.exists(F[k]) and os.path.exists(P[k]), k

# (tag, deck key, n games per arm, depth or None, budget or None, pinned, 2HG)
shapes = []
for k in DECKS:
    shapes.append((f'{k}play', k, N_PLAY, None, None, False, False))
    if k in TWO_HG:
        shapes.append((f'{k}2hgplay', k, N_PLAY, None, None, False, True))
for k in DECKS:
    shapes.append((f'{k}d5b20', k, N_SEARCH, 5, 20, True, False))
    shapes.append((f'{k}d3b10', k, N_SEARCH, 3, 10, True, False))
for k in DECKS:
    shapes.append((f'{k}d0', k, N_D0, 0, 0, True, False))

jobs = []
for (tag, k, n, d, b, pinned, hg) in shapes:
    for off in range(0, n, CHUNK):
        for arm, flags in ARMS.items():
            j = {"name": f"{arm}__{tag}__o{off}", "deck": F[k], "profile": P[k], "games": min(CHUNK, n - off),
                 "seed": base + off, "game_index": off, "flags": dict(flags)}
            if d is not None:
                j["depth"] = d
            if b is not None:
                j["budget_ms"] = b
            if pinned:
                j["ignore_play_profile"] = True
            if hg:
                j.update(HG)
            jobs.append(j)

games = sum(j['games'] for j in jobs)
if opt.estimate:
    for (tag, k, n, d, b, pinned, hg) in shapes:
        print(f'{tag:22s} n/arm={n:5d} depth={d} budget={b}')
else:
    json.dump({"jobs": jobs}, open(out, 'w'), indent=0)
print(len(shapes), 'shapes', len(jobs), 'jobs', games, 'games', f'(base {base})')
