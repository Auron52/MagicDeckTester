#!/usr/bin/env python3
"""gdiff.py <dirA> <dirB> [--full]: per-turn action diff of two single-game JSON logs."""
import json, sys, glob, os

def load(d):
    fs = sorted(glob.glob(os.path.join(d, '*.json')))
    return json.load(open(fs[-1]))

def fmt_act(a):
    s = a.get('type', '?')
    if 'cardName' in a: s += ' ' + a['cardName']
    for k in ('target', 'targetName', 'mode', 'x', 'X', 'detail', 'note', 'choice', 'tappedFor', 'paidWith'):
        if k in a: s += f' {k}={a[k]}'
    extra = {k: v for k, v in a.items() if k not in ('type', 'card', 'cardName', 'target', 'targetName', 'mode', 'x', 'X', 'detail', 'note', 'choice', 'tappedFor', 'paidWith')}
    if extra: s += ' ' + json.dumps(extra)[:200]
    return s

def turns(g):
    out = {}
    for t in g['turns']:
        key = (t['turn'], t.get('phase', ''))
        out.setdefault(t['turn'], []).append(t)
    return out

def summarize(tlist):
    lines = []
    for t in tlist:
        lines.append(f"  [{t.get('phase','')}]")
        for a in t.get('actions', []):
            lines.append('    ' + fmt_act(a))
        b = t.get('boardAfter', {})
        names = [c['cardName'] + ('(T)' if c.get('tapped') else '') for c in b.get('battlefield', [])]
        lines.append(f"    bf: {', '.join(names)}")
        lines.append(f"    hand: {b.get('hand')}  opp={b.get('opponentLife')} gy={b.get('graveyard')}")
    return lines

A = load(sys.argv[1]); B = load(sys.argv[2])
full = '--full' in sys.argv
num = {}
for name, ids in A['cardNumbering'].items():
    for i in ids: num[i] = name
def hn(h): return [num.get(i, i) if isinstance(i, int) else i for i in h]
print('A result', A['result'], 'B result', B['result'])
print('A opening', [c['cardName'] for c in A['openingHand']])
print('B opening', [c['cardName'] for c in B['openingHand']])
print('A mull', len(A['mulliganSequence']), 'B mull', len(B['mulliganSequence']))
ta = turns(A); tb = turns(B)
first = None
for n in sorted(set(ta) | set(tb)):
    sa = summarize(ta.get(n, [])); sb = summarize(tb.get(n, []))
    same = sa == sb
    if not same and first is None: first = n
    if full or not same:
        print(f'===== turn {n} {"SAME" if same else "DIFF"}')
        if same:
            print('\n'.join(sa))
        else:
            print('--- A'); print('\n'.join(sa))
            print('--- B'); print('\n'.join(sb))
print('first diverging turn', first)
# hand names at each turn end
