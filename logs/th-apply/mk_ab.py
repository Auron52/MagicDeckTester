#!/usr/bin/env python3
# mk_ab.py (th-apply, from logs/worldfire-ad/mk_ab.py): held-out paired PLAY-SETTING A/B of MTG_LINE_KEEP_CRIT_FILTER,
# off vs on, ONE pooled `mtg --batch` manifest. cwd = worktree.
#   usage: python3 logs/th-apply/mk_ab.py <out.json> [--base N] [--sizes k=std/2hg,...]
# Paired: game gi -> seed BASE + gi, game_index gi on both arms (arms interleaved per chunk). Depth AND budget OMITTED
# (each deck's profile picks both). 2HG = 30 team life, two heads.
import json, os, re, argparse
ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--base', type=int, default=5130000)
ap.add_argument('--sizes', default='th=4000/2000,cyclone=1000/300,niv=1000/300,morph=1000/300,sea=1000/300,hinata=400/150')
ap.add_argument('--chunk', type=int, default=25)
ap.add_argument('--start', type=int, default=0)
o = ap.parse_args()
L = ['MTG_LINE_KEEP_CRIT_FILTER']
ARMS = {'off': {k: False for k in L}, 'on': {k: True for k in L}}
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
shapes = []
for kv in o.sizes.split(','):
    k, v = kv.split('=')
    n_std, n_hg = (int(x) for x in v.split('/'))
    assert k in F and os.path.exists(F[k]) and os.path.exists(P[k]), k
    if n_std: shapes.append((k + 'play', k, n_std, False))
    if n_hg: shapes.append((k + '2hgplay', k, n_hg, True))
jobs = []
for (tag, k, n, hg) in shapes:
    for off in range(o.start, o.start + n, o.chunk):
        for arm, fl in ARMS.items():
            j = {"name": f"{arm}__{tag}__o{off}", "deck": F[k], "profile": P[k], "games": min(o.chunk, o.start + n - off),
                 "seed": o.base + off, "game_index": off, "flags": dict(fl)}
            if hg:
                j.update(HG)
            jobs.append(j)
# costliest first so the pool's tail is short
cost = {'hinata': 19, 'mazesend': 5, 'snow': 3.9, 'cyclone': 3.2, 'niv': 1.8, 'sea': 1.2, 'bruna': 0.8, 'morph': 0.7, 'th': 0.3, 'galadriel': 0.2, 'fluctuator': 0.15, 'breaching': 0.03}
jobs.sort(key=lambda j: -cost.get(j['name'].split('__')[1].replace('2hgplay', '').replace('play', ''), 1))
json.dump({"jobs": jobs}, open(o.out, 'w'), indent=0)
print(len(shapes), 'shapes', len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games', '(base %d)' % o.base)
