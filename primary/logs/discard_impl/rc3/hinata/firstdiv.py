#!/usr/bin/env python3
"""firstdiv.py <oldlogdir> <newlogdir> [ctx]: print the first diverging phase of two game logs, with both arms'
actions at that phase (+ ctx following phases)."""
import json, sys, glob, os

def load(p):
    if os.path.isdir(p):
        p = sorted(glob.glob(os.path.join(p, '*.json')))[0]
    return json.load(open(p))

def num_map(g):
    num = {}
    for nm, ids in g.get('cardNumbering', {}).items():
        for i in (ids if isinstance(ids, list) else [ids]):
            num[i] = nm
    return num

def fa(a):
    t = a.get('type', '?'); nm = a.get('cardName', '')
    ex = []
    for k in ('chosenX', 'manaPaid'):
        if k in a: ex.append(f'{k}={a[k]}')
    if t == 'REVEAL':
        lk = a.get('lookedAt', [])
        ex.append('src=' + str(a.get('source')) + ' looked=' + ','.join(x.get('cardName', '?') for x in lk))
    if t == 'ATTACK':
        ex.append(f"dmg={a.get('damage')} opp={a.get('oppLife')}")
    return f'{t}:{nm}' + (' {' + '; '.join(ex) + '}' if ex else '')

def sig(ph):
    return json.dumps(ph.get('actions', []), sort_keys=True)

def board(g, ph):
    num = num_map(g)
    b = ph.get('boardAfter', {})
    return (f"opp={b.get('opponentLife')} hand={[num.get(h, h) for h in b.get('hand', [])]} "
            f"staged={[num.get(h, h) if not isinstance(h, dict) else h.get('cardName') for h in b.get('staged', [])]} "
            f"bf={[x.get('cardName') + ('(T)' if x.get('tapped') else '') for x in b.get('battlefield', [])]}")

def main():
    go, gn = load(sys.argv[1]), load(sys.argv[2])
    ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    print('result old', go.get('result'), ' new', gn.get('result'))
    mo = json.dumps(go.get('mulliganSequence')) == json.dumps(gn.get('mulliganSequence'))
    print('mulligan identical:', mo)
    to, tn = go['turns'], gn['turns']
    i = 0
    while i < min(len(to), len(tn)) and sig(to[i]) == sig(tn[i]) and to[i]['phase'] == tn[i]['phase']:
        i += 1
    if i > 0:
        print(f"prev phase T{to[i-1]['turn']} {to[i-1]['phase']} board: {board(go, to[i-1])}")
    for j in range(i, i + ctx):
        for name, g, ts in (('OLD', go, to), ('NEW', gn, tn)):
            if j < len(ts):
                ph = ts[j]
                print(f"{name} T{ph['turn']} {ph['phase']}: " + ' | '.join(fa(a) for a in ph.get('actions', [])))
                print(f"     -> {board(g, ph)}")

main()
