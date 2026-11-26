#!/usr/bin/env python3
# (dragons-eng copy of logs/th-apply/mk_slower.py; --arms a,b names the pair, e.g. goff,gon)
# mk_slower.py <ab.json> <ab_report --list-slower output> <out.json> [--nc]: replay every slower game of the A/B, both
# arms, at --depth 8 --budget-ms 0 (ignore_play_profile, abandon 30M units); --nc instead replays at the deck's play
# settings (run it with MTG_NC_SEARCH=1 in the env: the clairvoyance check). One job per (arm, game).
import json, re, sys
ab = json.load(open(sys.argv[1]))['jobs']
ARMS = ('off', 'on')
for i, x in enumerate(sys.argv):
    if x == '--arms':
        ARMS = tuple(sys.argv[i + 1].split(','))
nc = '--nc' in sys.argv
by = {}
for j in ab:
    arm, shape, _ = j['name'].split('__')
    for k in range(j['games']):
        by[(arm, shape, j['game_index'] + k)] = j
jobs = []
for line in open(sys.argv[2]):
    m = re.match(r'SLOWER (\S+) gi=(\d+) off=(-?\d+) on=(-?\d+)', line)
    if not m:
        continue
    shape, gi = m.group(1), int(m.group(2))
    for arm in ARMS:
        src = by[(arm, shape, gi)]
        j = {k: v for k, v in src.items() if k != 'name'}
        j.update(name='%s__%s__gi%d' % (arm, shape, gi), games=1, seed=src['seed'] - src['game_index'] + gi, game_index=gi)
        j['flags'] = dict(src['flags'])
        if not nc:   # --nc: run the batch with MTG_NC_SEARCH=1 in the env (not a heurarm slot)
            j.update(depth=8, budget_ms=0, ignore_play_profile=True, abandon_units=30000000)
        jobs.append(j)
json.dump({'jobs': jobs}, open(sys.argv[3], 'w'), indent=1)
print(len(jobs), 'jobs')
