"""Raw trace actions for turns <= T of one game, both arms. Usage: raw.py <stem> <gi> <T>"""
import json, sys
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
stem, gi, T = sys.argv[1], sys.argv[2], int(sys.argv[3])
for arm in ('trold', 'trnew'):
    d = json.load(open(f'{A}/{arm}/traces/{stem}_tr_gi{gi}.json'))
    print('==', arm, 'keys:', list(d.keys())[:12])
    for t in d.get('turns', []):
        if t.get('turn') > T:
            break
        for a in t.get('actions', []):
            s = {k: v for k, v in a.items() if k not in ('boardAfter',)}
            print(' T%s' % t.get('turn'), json.dumps(s)[:240])
