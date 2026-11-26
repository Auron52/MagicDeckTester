#!/usr/bin/env python3
# mk_ab.py (dragons-eng, from logs/th-apply/mk_ab.py): held-out paired PLAY-SETTING A/B of the two dragons-eng levers, ONE
# pooled `mtg --batch` manifest. cwd = worktree.
#   gy  arms goff / gon : MTG_GYRETURN_TAP_DEBIT   (decks with a gy-return source: dragons, landfall, kittyv3)
#   dig arms doff / don : MTG_DIG_FLOAT_HAND_DEMAND (decks with a sacrifice-draw dig source)
# The other lever stays at its default (ON) in each pair. Paired: game gi -> seed BASE + gi, game_index gi on both arms
# (arms interleaved per chunk). Depth AND budget OMITTED (each deck's profile picks both). 2HG = 30 team life, two heads.
#   usage: python3 logs/dragons-eng/mk_ab.py <out.json> [--base N] [--scale F]
import json, os, re, argparse
ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--base', type=int, default=5260000)
ap.add_argument('--scale', type=float, default=1.0)
ap.add_argument('--chunk', type=int, default=25)
o = ap.parse_args()
LEVERS = {'gy': 'MTG_GYRETURN_TAP_DEBIT', 'dig': 'MTG_DIG_FLOAT_HAND_DEMAND'}
ARMS = {'gy': {'goff': False, 'gon': True}, 'dig': {'doff': False, 'don': True}}
SIZES = {
    'gy':  {'dragons': (6000, 2000), 'landfall': (1500, 500), 'kittyv3': (300, 100)},
    'dig': {'dragons': (6000, 2000), 'midrangeburn': (600, 200), 'onedropburn': (1000, 300), 'modernrwburn': (600, 200),
            'merfolk': (1500, 500), 'th': (2000, 600), 'auras': (2000, 600), 'pirates': (1500, 500),
            'selesnyav2': (1500, 500), 'soldiers': (1500, 500), 'elementals': (600, 200), 'extraturns': (300, 100),
            'redelem': (1500, 500), 'stuffy': (300, 100)},
}
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
cost = json.load(open('test/suite_cost.json'))
cost = cost.get('decks', cost)
jobs = []
nshape = 0
for lev, decks in SIZES.items():
    for k, (n_std, n_hg) in decks.items():
        assert k in F and os.path.exists(F[k]) and os.path.exists(P[k]), k
        for tag, n, hg in ((k + lev + 'play', n_std, False), (k + lev + '2hgplay', n_hg, True)):
            n = int(n * o.scale)
            if n <= 0:
                continue
            nshape += 1
            for off in range(0, n, o.chunk):
                for arm, on in ARMS[lev].items():
                    j = {"name": f"{arm}__{tag}__o{off}", "deck": F[k], "profile": P[k], "games": min(o.chunk, n - off),
                         "seed": o.base + off, "game_index": off, "flags": {LEVERS[lev]: on}}
                    if hg:
                        j.update(HG)
                    j['_c'] = cost.get(k, {}).get('cost_per_game_ms', 500) * (2 if hg else 1)
                    jobs.append(j)
jobs.sort(key=lambda j: -j['_c'])   # costliest first so the pool's tail is short
est = sum(j['_c'] * j['games'] for j in jobs) / 1000.0
for j in jobs:
    del j['_c']
json.dump({"jobs": jobs}, open(o.out, 'w'), indent=0)
print(nshape, 'shapes', len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games', '(base %d)' % o.base,
      'est %.0f core-s (%.0f min on 32)' % (est, est / 32 / 60))
