"""Case-setting single-game trace jobs (both arms) for every game in d8b0.idx -- draws/kept-hand check."""
import json
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
m = json.load(open(A + '/d8b0.json'))
case = [j for j in m['jobs'] if j['name'].endswith('_case')]
for j in case:
    j['name'] = j['name'][:-5] + '_tr'
json.dump({'jobs': case}, open(A + '/trace_new.json', 'w'), indent=0)
json.dump({'jobs': [j for j in case if not j['name'].startswith('redelem')]}, open(A + '/trace_old.json', 'w'), indent=0)
print(len(case))
