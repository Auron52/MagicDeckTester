#!/usr/bin/env python3
"""show.py <game.json> [<game2.json>]: compact per-turn action listing (and diff of two)."""
import json, sys, glob, os

def load(p):
    if os.path.isdir(p):
        p = sorted(glob.glob(os.path.join(p, '*.json')))[0]
    return json.load(open(p))

def fmt_action(a):
    t = a.get('type', '?')
    nm = a.get('cardName', '')
    extra = []
    for k, v in a.items():
        if k in ('type', 'cardName', 'card'):
            continue
        if isinstance(v, (dict, list)) and len(json.dumps(v)) > 120:
            v = json.dumps(v)[:120] + '...'
        extra.append(f'{k}={v}')
    s = f'{t}:{nm}'
    if extra:
        s += ' {' + ', '.join(str(e) for e in extra) + '}'
    return s

def per_turn(g):
    num = {}
    for nm, ids in g.get('cardNumbering', {}).items():
        for i in (ids if isinstance(ids, list) else [ids]):
            num[i] = nm
    out = {}
    for ph in g['turns']:
        key = (ph['turn'])
        lst = out.setdefault(key, [])
        lst.append(f"  [{ph.get('phase')}]")
        for a in ph.get('actions', []):
            lst.append('    ' + fmt_action(a))
        b = ph.get('boardAfter', {})
        if ph.get('phase') in ('END', 'CLEANUP', 'END_STEP') or True:
            hand = b.get('hand', [])
            bf = [x.get('cardName') + ('(T)' if x.get('tapped') else '') for x in b.get('battlefield', [])]
            lst.append(f"    -> opp={b.get('opponentLife')} hand={[num.get(h, h) for h in hand]} bf={bf}")
    return out

def main():
    gs = [load(p) for p in sys.argv[1:]]
    for g in gs:
        print('result', g.get('result'), 'mull', g.get('mulliganSequence'))
        print('opening', [c.get('cardName') if isinstance(c, dict) else c for c in g.get('openingHand', [])])
    pts = [per_turn(g) for g in gs]
    turns = sorted(set().union(*[set(p) for p in pts]))
    for t in turns:
        print(f'===== TURN {t}')
        for i, p in enumerate(pts):
            print(f'--- arm{i}')
            for line in p.get(t, []):
                print(line)

main()
