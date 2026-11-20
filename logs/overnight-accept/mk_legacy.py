"""Case jobs (+ chosen d8b0 jobs) with legacy_passive_opponent_lands=true -> legacy.json"""
import json
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
skip = ('gods', 'gods2hg', 'spirits', 'redelem')
d8 = {'hinata_overnight_d5_s5005_gi384', 'hinata_overnight_d5_s6006_gi156', 'pd_overnight_d5_s6006_gi47',
      'alchemist_overnight_d5_s6006_gi318', 'galadriel_overnight_d5_s4004_gi1235'}
m = json.load(open(A + '/d8b0.json'))
jobs = []
for j in m['jobs']:
    stem, kind = j['name'].rsplit('_', 1)
    if stem.split('_')[0] in skip:
        continue
    if kind == 'case' or stem in d8:
        jobs.append(dict(j, legacy_passive_opponent_lands=True))
json.dump({'jobs': jobs}, open(A + '/legacy.json', 'w'), indent=0)
print(len(jobs))
