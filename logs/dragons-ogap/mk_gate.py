#!/usr/bin/env python3
# mk_gate.py: the Dragons slice of mana-clean gate claude-queue-20261125a (seed 30000000 + gi, gi 96000-195999, the
# gate's own 250-game chunks), re-run under MTG_MANA_ORACLE=2. ONE pooled manifest per binary.
#   usage: python3 mk_gate.py <gate manifest.on.0.json> <out.json> [--n GAMES]
import json, argparse
ap = argparse.ArgumentParser()
ap.add_argument('gate'); ap.add_argument('out')
ap.add_argument('--n', type=int, default=100000)
o = ap.parse_args()
jobs = [j for j in json.load(open(o.gate))['jobs'] if j['name'].startswith('dragons__') and j['game_index'] - 96000 < o.n]
json.dump({'jobs': jobs}, open(o.out, 'w'), indent=0)
print(len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games')
