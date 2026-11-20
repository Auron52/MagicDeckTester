"""Artifact-swap rows: the run's own jobs for a deck, with the deck folder swapped for a variant copy."""
import json
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
V = 'logs/overnight-accept/v'
m = json.load(open('/workspaces/MagicDeckTester/test/logs/overnight/manifest.json'))
out = []
for j in m['jobs']:
    n = j['name']
    pre = n.split('_')[0]
    var = []
    if pre in ('hinata', 'hinata2hg') and '_d5_' in n and j['budget_ms'] > 0:
        var = [('HinataOldArt', 'Hinata')]
    if pre in ('spirits', 'spirits2hg') and j['budget_ms'] > 0 and '_d0_' not in n:
        var = [('SpiritsOldArt', 'Spirits'), ('SpiritsNoValue', 'Spirits')]
    for v, stem in var:
        k = dict(j, name=f'{n}__{v}', deck=f'{V}/{v}/{stem}.cod', profile=f'{V}/{v}/{stem}.profile.json')
        k.pop('weight', None)
        out.append(k)
json.dump({'jobs': out}, open(A + '/art.json', 'w'), indent=0)
print(len(out), 'jobs', sum(j['games'] for j in out), 'games')
