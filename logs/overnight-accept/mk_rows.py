"""Whole run rows matching a prefix list, with extra job fields. Usage: mk_rows.py <out> '<json extra>' <name-prefix>..."""
import json, sys
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
out, extra, pres = sys.argv[1], json.loads(sys.argv[2]), tuple(sys.argv[3:])
m = json.load(open('/workspaces/MagicDeckTester/test/logs/overnight/manifest.json'))
jobs = []
for j in m['jobs']:
    if j['name'].startswith(pres):
        k = dict(j, **extra)
        k.pop('weight', None)
        jobs.append(k)
json.dump({'jobs': jobs}, open(f'{A}/{out}.json', 'w'), indent=0)
print(len(jobs), sum(j['games'] for j in jobs))
