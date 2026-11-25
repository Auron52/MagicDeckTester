#!/usr/bin/env python3
# mk_ab.py (2026-11-25): held-out paired PLAY-SETTING A/B of the three line-keep reach levers (MTG_LINE_KEEP_CRIT_MULTI,
# MTG_LINE_KEEP_SUSPEND, MTG_LINE_KEEP_RITUAL), all off vs all on, ONE pooled `mtg --batch` manifest. cwd = worktree.
#   usage: python3 logs/worldfire-ad/mk_ab.py <out.json> [--base N] [--n N] [--wf N] [--decks k1,k2,...]
# Paired: game gi -> seed BASE + gi, game_index gi on both arms (arms interleaved per chunk). Depth AND budget OMITTED
# (each deck's enabled value_play block picks both). Worldfire also plays 2HG (30 team life, two heads).
import json, os, re, argparse
ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--base', type=int, default=2710000)
ap.add_argument('--n', type=int, default=400, help='games per arm, every deck but Worldfire')
ap.add_argument('--wf', type=int, default=3000, help='games per arm, Worldfire standard and 2HG')
ap.add_argument('--hg', type=int, default=0, help='games per arm in 2HG, every deck but Worldfire (0 = none)')
ap.add_argument('--decks', default='', help='comma list (default: every suite deck)')
ap.add_argument('--chunk', type=int, default=25)
o = ap.parse_args()
L = ['MTG_LINE_KEEP_CRIT_MULTI', 'MTG_LINE_KEEP_SUSPEND', 'MTG_LINE_KEEP_RITUAL']
ARMS = {'off': {k: False for k in L}, 'on': {k: True for k in L}}
# 'loose' (ritual decks only): all three on with MTG_LINE_KEEP_RITUAL_TIGHT off -- the ritual rung before its exact
# cheaper form (gate counts the float, tight joint), to measure what the gate's float costs in quality.
LOOSE = dict({k: True for k in L}, MTG_LINE_KEEP_RITUAL_TIGHT=False)
RITUAL_DECKS = {'cyclone', 'dragonstorm', 'hinata', 'shadowborn', 'worldfire'}
HG = {"starting_life": 30, "opponent_heads": 2}
src = open('test/regression_cases.sh').read()
def amap(name):
    m = re.search(r'^declare -A %s=\((.*?)^\)' % name, src, re.S | re.M)
    out = {}
    for line in m.group(1).splitlines():
        mm = re.match(r'\s+\[(\w+)\]=(".*?"|\S+)', line)
        if mm:
            out[mm.group(1)] = mm.group(2).strip('"')
    return out
F, P = amap('DECK_FILE'), amap('DECK_PROF')
decks = [d for d in o.decks.split(',') if d] or sorted(F)
shapes = []
for k in decks:
    assert k in F and os.path.exists(F[k]) and os.path.exists(P[k]), k
    n = o.wf if k == 'worldfire' else o.n
    shapes.append((k + 'play', k, n, False))
    if k == 'worldfire':
        shapes.append((k + '2hgplay', k, n, True))
    elif o.hg > 0:
        shapes.append((k + '2hgplay', k, o.hg, True))
jobs = []
for (tag, k, n, hg) in shapes:
    for off in range(0, n, o.chunk):
        arms = dict(ARMS)
        if k in RITUAL_DECKS:
            arms['loose'] = LOOSE
        for arm, fl in arms.items():
            j = {"name": f"{arm}__{tag}__o{off}", "deck": F[k], "profile": P[k], "games": min(o.chunk, n - off),
                 "seed": o.base + off, "game_index": off, "flags": dict(fl)}
            if hg:
                j.update(HG)
            jobs.append(j)
json.dump({"jobs": jobs}, open(o.out, 'w'), indent=0)
print(len(shapes), 'shapes', len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games', '(base %d)' % o.base)
