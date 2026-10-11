#!/usr/bin/env python3
"""Dump a game log: per turn/phase actions, plus hand/board after each phase, names resolved."""
import json, sys, glob

def load(path):
    if not path.endswith('.json'):
        path = glob.glob(path + '/*.json')[0]
    return json.load(open(path))

def main():
    d = load(sys.argv[1])
    num = {}
    cn = d['cardNumbering']
    if isinstance(cn, dict):
        for k, v in cn.items():
            for n in v:
                num[int(n)] = k
    else:
        for e in cn:
            num[int(e.get('number', e.get('card')))] = e.get('name')
    nm = lambda c: f"{num.get(c, '?')}#{c}"
    print('opening', [nm(c) for c in d['openingHand']] if d.get('openingHand') and isinstance(d['openingHand'][0], int) else d.get('openingHand'))
    print('mull', d.get('mulliganSequence'))
    print('result', d.get('result'))
    for t in d['turns']:
        acts = []
        for a in t.get('actions', []):
            s = a.get('type')
            if 'cardName' in a:
                s += ':' + a['cardName'] + '#' + str(a.get('card'))
            extra = {k: v for k, v in a.items() if k not in ('type', 'card', 'cardName', 'boardAfter')}
            if extra:
                s += ' ' + json.dumps(extra)[:300]
            acts.append(s)
        b = t.get('boardAfter', {})
        print(f"T{t['turn']} {t['phase']}: " + ' | '.join(acts))
        if b:
            bf = []
            for c in b.get('battlefield', []):
                if isinstance(c, dict):
                    bf.append(c.get('cardName', '?') + ('(T)' if c.get('tapped') else ''))
                else:
                    bf.append(nm(c))
            print('    hand:', [nm(c) if isinstance(c, int) else c for c in b.get('hand', [])])
            print('    bf  :', bf)
            print('    gy  :', [nm(c) if isinstance(c, int) else c for c in b.get('graveyard', [])],
                  'opp', b.get('opponentLife'), 'me', b.get('playerLife'))

main()
