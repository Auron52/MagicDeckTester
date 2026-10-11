#!/usr/bin/env python3
"""Point 10 (MTG_ATTACK_REPROJECT_INTENT) measurement, ONE pooled manifest:
  * held-out paired on vs noINTENT on the SAME seeds/chunks as heldout_final (51xxxxx d0 / 56xxxxx d3),
    so the noINTENT arm's digests can also be compared chunk-for-chunk against heldout_final's `on` arm
    (lever=0 must reproduce the committed tip);
  * the identity jobs of spec_ida_mine.json (10 decks, x = every new lever off, now incl. INTENT), to
    compare against ida_head/out (the pre-fix head binary).
Writes <outdir>/m.json and <outdir>/spec.json (the held-out half, for ab.py report)."""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
prev = json.load(open(os.path.join(HERE, 'heldout_final', 'spec.json')))
spec = {'decks': prev['decks'], 'cells': prev['cells'],
        'arms': {'on': [], 'noINTENT': ['MTG_ATTACK_REPROJECT_INTENT']},
        'deck_arms': {d: ['on', 'noINTENT'] for d in prev['decks']},
        'deck_cells': prev['deck_cells']}
ida = json.load(open(os.path.join(HERE, 'spec_ida_mine.json')))
ida['arms']['x'] = ida['arms']['x'] + ['MTG_ATTACK_REPROJECT_INTENT']
tmp_h = os.path.join(out, 'h')
tmp_i = os.path.join(out, 'i')
json.dump(spec, open(os.path.join(out, 'spec_h.json'), 'w'), indent=1)
json.dump(ida, open(os.path.join(out, 'spec_i.json'), 'w'), indent=1)
ab = os.path.join(HERE, 'ab.py')
subprocess.check_call([sys.executable, ab, 'mk', tmp_h, os.path.join(out, 'spec_h.json')])
subprocess.check_call([sys.executable, ab, 'mk', tmp_i, os.path.join(out, 'spec_i.json')])
jobs = json.load(open(os.path.join(tmp_h, 'm.json')))['jobs'] + json.load(open(os.path.join(tmp_i, 'm.json')))['jobs']
names = [j['name'] for j in jobs]
assert len(names) == len(set(names)), 'duplicate job names'
jobs.sort(key=lambda j: 0 if j.get('depth', 0) > 0 or 'depth' not in j else 1)
json.dump({'jobs': jobs}, open(os.path.join(out, 'm.json'), 'w'))
json.dump(spec, open(os.path.join(out, 'spec.json'), 'w'), indent=1)
print('%d jobs, %d games -> %s' % (len(jobs), sum(j['games'] for j in jobs), os.path.join(out, 'm.json')))
