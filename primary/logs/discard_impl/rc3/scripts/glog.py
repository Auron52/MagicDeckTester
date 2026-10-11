#!/usr/bin/env python3
"""Compact print of a game JSON log (one line per action, board summary per phase).
usage: glog.py <game.json> [--board]"""
import json, sys, glob, os

def load(p):
    if os.path.isdir(p):
        fs = sorted(glob.glob(os.path.join(p, '*.json')))
        fs = [f for f in fs if not f.endswith('census.json')]
        p = fs[0]
    return json.load(open(p))

def main():
    g = load(sys.argv[1])
    board = '--board' in sys.argv
    names = {}
    for n, ids in g['cardNumbering'].items():
        for i in ids:
            names[i] = n
    print('opening:', [x['cardName'] for x in g['openingHand']], 'mulls:', len(g['mulliganSequence']) - 1)
    print('result:', g['result'])
    for t in g['turns']:
        acts = []
        for a in t['actions']:
            ty = a.get('type')
            if ty == 'UNTAP_SOURCES':
                continue
            s = ty
            if 'cardName' in a:
                s += ':' + a['cardName']
            if 'cards' in a and ty != 'UNTAP_SOURCES':
                s += ':' + ','.join(names.get(c, str(c)) if isinstance(c, int) else str(c) for c in a['cards'])
            for k in ('ability', 'manaPaid', 'damage', 'oppLife', 'source', 'kept', 'bottomed', 'lookedAt'):
                if k in a:
                    v = a[k]
                    if isinstance(v, list) and v and isinstance(v[0], int):
                        v = [names.get(c, c) for c in v]
                    s += ' %s=%s' % (k, v)
            acts.append(s)
        b = t['boardAfter']
        line = 'T%d %-8s %s' % (t['turn'], t['phase'], ' | '.join(acts))
        print(line)
        if board and t['phase'] in ('MAIN_2', 'END', 'MAIN_1'):
            bf = [(x['cardName'] + ('*' if x.get('tapped') else '')) for x in b['battlefield']]
            hand = [names.get(c, c) for c in b['hand']]
            print('     bf=%s' % bf)
            print('     hand=%s opp=%s' % (hand, b.get('opponentLife')))

main()
