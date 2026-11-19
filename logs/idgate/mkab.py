#!/usr/bin/env python3
"""mkab.py <in manifest> <out manifest> [--chunk N]: duplicate each job into arms off/on of MTG_ID_GATE_TRIAL,
arms innermost, arm order alternated per (job, chunk). --chunk N splits each job into chunks of N games
(seed = base + offset, game_index offset kept)."""
import json, sys
src, dst = sys.argv[1], sys.argv[2]
chunk = int(sys.argv[sys.argv.index('--chunk') + 1]) if '--chunk' in sys.argv else 0
jobs = json.load(open(src))['jobs']; out = []; flip = 0
for j in jobs:
    parts = [(0, j['games'])]
    if chunk and j['games'] > chunk:
        parts = [(o, min(chunk, j['games'] - o)) for o in range(0, j['games'], chunk)]
    for o, n in parts:
        arms = [('off', False), ('on', True)]
        if flip % 2: arms.reverse()
        flip += 1
        for a, v in arms:
            k = dict(j); k['name'] = f"{a}__{j['name']}" + (f"_o{o}" if len(parts) > 1 else '')
            k['flags'] = {'MTG_ID_GATE_TRIAL': v}
            if len(parts) > 1:
                k['seed'] = j['seed'] + o; k['game_index'] = o; k['games'] = n
            out.append(k)
json.dump({'jobs': out}, open(dst, 'w'), indent=0)
print(len(out), 'jobs')
