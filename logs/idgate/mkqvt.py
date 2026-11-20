#!/usr/bin/env python3
"""mkqvt.py <in manifest> <out manifest> [--chunk N] [--mults 1.1,1.2]: the QUALITY-VS-TIME held-out manifest.
Per job (chunked; seed = base + offset, game_index = offset) the arms, innermost and order-rotated per chunk:
  off__  trial off at the job's budget b          (the base)
  on__   trial on  at b                           (the change)
  offxNN__ trial off at round(b * m) for each m    (the base's own budget curve, for the equal-cost comparison)
Every arm sets MTG_ID_GATE_TRIAL explicitly. Run it with MTG_PGO=0 (one binary for every arm)."""
import json, sys
src, dst = sys.argv[1], sys.argv[2]
chunk = int(sys.argv[sys.argv.index('--chunk') + 1]) if '--chunk' in sys.argv else 25
mults = [float(x) for x in (sys.argv[sys.argv.index('--mults') + 1] if '--mults' in sys.argv else '1.1,1.2').split(',')]
jobs = json.load(open(src))['jobs']; out = []; rot = 0
for j in jobs:
    b = j['budget_ms']; assert b > 0, j['name']
    arms = [('off', False, b), ('on', True, b)]
    prev = b
    for m in mults:
        bm = max(prev + 1, int(round(b * m))); prev = bm
        arms.append((f"offx{int(round(m * 100))}", False, bm))
    parts = [(o, min(chunk, j['games'] - o)) for o in range(0, j['games'], chunk)] if j['games'] > chunk else [(0, j['games'])]
    for o, n in parts:
        r = rot % len(arms); rot += 1
        for a, v, bb in arms[r:] + arms[:r]:
            k = dict(j); k['name'] = f"{a}__{j['name']}" + (f"_o{o}" if len(parts) > 1 else '')
            k['flags'] = dict(j.get('flags', {}), MTG_ID_GATE_TRIAL=v); k['budget_ms'] = bb
            if len(parts) > 1: k['seed'] = j['seed'] + o; k['game_index'] = o; k['games'] = n
            out.append(k)
json.dump({'jobs': out}, open(dst, 'w'), indent=0)
print(len(out), 'jobs', sum(k['games'] for k in out), 'games; arms', [a for a, _, _ in arms])
