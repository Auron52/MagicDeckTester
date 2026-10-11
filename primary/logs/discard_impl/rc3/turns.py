#!/usr/bin/env python3
"""turns.py <gamedir> <from> <to>: detailed per-phase dump (actions + board with attachments/tapped)."""
import json, glob, os, sys
g = json.load(open(sorted(glob.glob(os.path.join(sys.argv[1], '*.json')))[-1]))
a, b = int(sys.argv[2]), int(sys.argv[3])
num = {}
for n, ids in g['cardNumbering'].items():
    for i in ids: num[i] = n
print('result', g['result'])
for t in g['turns']:
    if not (a <= t['turn'] <= b):
        continue
    print(f"T{t['turn']} {t.get('phase')}")
    for x in t.get('actions', []):
        extra = {k: v for k, v in x.items() if k not in ('type', 'card', 'cardName')}
        print('   ', x.get('type'), x.get('cardName', ''), json.dumps(extra)[:300] if extra else '')
    bd = t['boardAfter']
    names = {c['card']: c['cardName'] for c in bd['battlefield']}
    bf = []
    for c in bd['battlefield']:
        s = c['cardName'] + ('(T)' if c.get('tapped') else '')
        if c.get('attachedTo') is not None:
            s += '->' + names.get(c['attachedTo'], str(c['attachedTo']))
        if c.get('counters'):
            s += str(c['counters'])
        bf.append(s)
    print('    bf:', ', '.join(bf))
    print('    hand:', [num.get(i, i) for i in bd['hand']], 'opp', bd.get('opponentLife'), 'gy', [num.get(i, i) for i in bd['graveyard']])
