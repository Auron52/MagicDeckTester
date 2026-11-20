"""Subset of d8b0.json (both d8b0 and case jobs) for the given deck prefixes -> <out>.json"""
import json, sys
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
out, kinds, pre = sys.argv[1], sys.argv[2].split(','), tuple(sys.argv[3:])
m = json.load(open(A + '/d8b0.json'))
jobs = [j for j in m['jobs'] if j['name'].split('_')[0] in pre and j['name'].rsplit('_', 1)[1] in kinds]
json.dump({'jobs': jobs}, open(f'{A}/{out}.json', 'w'), indent=0)
print(len(jobs))
