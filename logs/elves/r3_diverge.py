#!/usr/bin/env python3
"""r3_diverge.py <dirA> <dirB>: the first turn/phase where two single-game logs differ, with both sides' actions
there (and the battlefield's tapped state after each cast, from the next boardAfter)."""
import json, sys, glob
def load(d):
    g = json.load(open(glob.glob(d + '/*.json')[0]))
    return g, [(t['turn'], t['phase'], t['actions'], t.get('boardAfter', {})) for t in g['turns']]
def fmt(acts):
    out = []
    for a in acts:
        s = a.get('type', '?') + ' ' + a.get('cardName', '')
        for k in ('manaPaid', 'kickCount', 'x', 'chosenX', 'damage', 'oppLife'):
            if k in a: s += f' {k}={a[k]}'
        out.append(s.strip())
    return out
ga, A = load(sys.argv[1]); gb, B = load(sys.argv[2])
print('result', ga['result'], 'vs', gb['result'])
for (ta, pa, aa, ba), (tb, pb, ab, bb) in zip(A, B):
    if (ta, pa) != (tb, pb) or fmt(aa) != fmt(ab):
        print(f'first divergence T{ta} {pa} / T{tb} {pb}')
        print('  A:', fmt(aa)); print('  B:', fmt(ab))
        for lab, b in (('A', ba), ('B', bb)):
            bf = [f"{c['cardName']}{'(T)' if c.get('tapped') else ''}" for c in b.get('battlefield', []) if not c.get('isLand')]
            print(f'  {lab} board after:', ' '.join(bf))
        break
