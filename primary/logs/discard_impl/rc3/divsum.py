#!/usr/bin/env python3
"""divsum.py <deck> <cell> <gi...>: first divergent turn summary, old vs new (from rc3/<deck>/g<gi>_<cell>_{old,new})."""
import json, glob, os, sys
K = '/workspaces/MagicDeckTester/logs/discard_impl/rc3'
deck, cell = sys.argv[1], sys.argv[2]

def load(d):
    return json.load(open(sorted(glob.glob(os.path.join(d, '*.json')))[-1]))

def phase_acts(t, num):
    out = []
    for a in t.get('actions', []):
        ty = a.get('type')
        if ty == 'DRAW':
            continue
        s = ty[:4] + ':' + a.get('cardName', '')
        if ty == 'ATTACK':
            s = f"ATK({a.get('damage')})"
        out.append(s)
    return out

for gi in sys.argv[3:]:
    A = load(f'{K}/{deck}/g{gi}_{cell}_old'); B = load(f'{K}/{deck}/g{gi}_{cell}_new')
    num = {}
    for n, ids in A['cardNumbering'].items():
        for i in ids: num[i] = n
    ta = {}; tb = {}
    for t in A['turns']: ta.setdefault(t['turn'], []).append(t)
    for t in B['turns']: tb.setdefault(t['turn'], []).append(t)
    first = None
    for n in sorted(set(ta) | set(tb)):
        if json.dumps(ta.get(n)) != json.dumps(tb.get(n)):
            first = n; break
    print(f'### {deck} {cell} gi={gi} old={A["result"].get("turn")} new={B["result"].get("turn")} first-div T{first}')
    if first is None: continue
    # state before the divergent turn (end of previous turn, from old)
    prev = ta.get(first - 1, [])
    if prev:
        b = prev[-1]['boardAfter']
        print('   prior bf:', ', '.join(c['cardName'] + ('(T)' if c.get('tapped') else '') for c in b['battlefield']))
        print('   prior hand:', [num.get(i, i) for i in b['hand']], 'opp', b.get('opponentLife'), 'life', b.get('playerLife'))
    for n in range(first, max(max(ta), max(tb)) + 1):
        for lab, T in (('old', ta), ('new', tb)):
            segs = []
            for t in T.get(n, []):
                if t.get('phase') == 'DRAW':
                    dr = [a.get('cardName') for a in t.get('actions', []) if a.get('type') == 'DRAW']
                    segs.append('draw ' + ','.join(dr))
                    continue
                acts = phase_acts(t, num)
                if acts: segs.append(t.get('phase', '')[:2] + '[' + ' '.join(acts) + ']')
            if T.get(n):
                opp = T[n][-1]['boardAfter'].get('opponentLife')
                print(f'   T{n} {lab}: ' + ' | '.join(segs) + f'  -> opp {opp}')
