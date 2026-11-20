import json, random, os, sys
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
OLDTREE = '/workspaces/MagicDeckTester/.claude/worktrees/ovn1120-old'
m = json.load(open('/workspaces/MagicDeckTester/test/logs/overnight/manifest.json'))
J = {j['name']: j for j in m['jobs']}
rows = {}
for l in open('/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/classify.out'):
    if 'PERSISTS' not in l:
        continue
    k, gi, o, n = l.split()[:4]
    rows[(k, int(gi[2:]))] = (o, n)
deck = lambda k: k.split('_')[0]
sel = [r for r in rows if deck(r[0]) not in ('midrangeburn', 'spirits')]
rnd = random.Random(1120)
for d in ('midrangeburn', 'spirits'):
    c = sorted(r for r in rows if deck(r[0]) == d)
    sel += rnd.sample(c, 10)
sel.sort()
jobs = []
idx = []
for k, gi in sel:
    j = J[k]
    o, n = rows[(k, gi)]
    if not os.path.exists(os.path.join(OLDTREE, j['deck'])):
        print('MISSING old deck', k, file=sys.stderr)
    base = {x: j[x] for x in ('deck', 'profile', 'starting_life', 'opponent_heads') if x in j}
    stem = f'{k}_gi{gi}'
    jobs.append(dict(base, name=stem + '_d8b0', games=1, seed=j['seed'] + gi, game_index=gi,
                     depth=8, budget_ms=0, ignore_play_profile=True))
    c = dict(base, name=stem + '_case', games=1, seed=j['seed'] + gi, game_index=gi,
             budget_ms=j['budget_ms'])
    if 'depth' in j:
        c['depth'] = j['depth']
        c['ignore_play_profile'] = j.get('ignore_play_profile', True)
    if 'abandon_units' in j:
        c['abandon_units'] = j['abandon_units']
    jobs.append(c)
    idx.append(f'{stem}\t{k}\t{gi}\t{o}\t{n}')
json.dump({'jobs': jobs}, open(A + '/d8b0.json', 'w'), indent=0)
open(A + '/d8b0.idx', 'w').write('\n'.join(idx) + '\n')
print(len(sel), 'games', len(jobs), 'jobs')
