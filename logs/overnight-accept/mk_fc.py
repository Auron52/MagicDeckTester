"""Five Colour d0 slower-game traces: appends idx rows + both-arm trace manifests (fc_tr_new/old.json)."""
import json
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
m = json.load(open('/workspaces/MagicDeckTester/test/logs/overnight/manifest.json'))
j = [x for x in m['jobs'] if x['name'] == 'fivecolour_overnight_d0_s4004'][0]
gis = {18: (6, 7), 35: (5, 6), 67: (5, 7), 192: (5, 7), 524: (5, 7), 553: (5, 7)}
jobs = []
idx = []
for gi, (o, n) in gis.items():
    stem = f'fivecolour_overnight_d0_s4004_gi{gi}'
    k = {x: j[x] for x in ('deck', 'profile', 'depth', 'budget_ms', 'ignore_play_profile') if x in j}
    jobs.append(dict(k, name=stem + '_tr', games=1, seed=j['seed'] + gi, game_index=gi))
    idx.append(f'{stem}\tfivecolour_overnight_d0_s4004\t{gi}\t{o}\t{n}')
json.dump({'jobs': jobs}, open(A + '/fc_tr.json', 'w'), indent=0)
open(A + '/fc.idx', 'w').write('\n'.join(idx) + '\n')
print(len(jobs))
