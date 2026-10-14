#!/usr/bin/env python3
"""showgame.py LOG.json [LOG2.json] -- compact per-turn action list (side by side if two)."""
import json, sys

def nm(x):
    if isinstance(x, dict):
        return x.get('cardName', str(x.get('card')))
    return str(x)

def lines(path):
    d = json.load(open(path))
    out = ['opening: ' + ', '.join(nm(c) for c in d.get('openingHand', []))]
    for t in d['turns']:
        acts = []
        for a in t.get('actions', []):
            typ = a.get('type', '?')
            s = '%s:%s' % (typ, a.get('cardName', a.get('source', '')))
            if 'manaPaid' in a:
                s += '[' + a['manaPaid'] + ']'
            for k in ('x', 'chosenX', 'targets', 'damage', 'kept', 'bottomed'):
                if k in a:
                    s += ' %s=%s' % (k, json.dumps(a[k]))
            acts.append(s)
        if not acts:
            continue
        b = t.get('boardAfter', {})
        out.append('T%s %-6s opp=%s | %s' % (t.get('turn'), t.get('phase', ''), b.get('opponentLife'), ' ; '.join(acts)))
    out.append('result: ' + json.dumps(d.get('result')))
    return out

a = lines(sys.argv[1])
if len(sys.argv) > 2:
    b = lines(sys.argv[2])
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else ''
        y = b[i] if i < len(b) else ''
        if x == y:
            print('   ', x)
        else:
            print('OLD', x)
            print('NEW', y)
else:
    print('\n'.join(a))
