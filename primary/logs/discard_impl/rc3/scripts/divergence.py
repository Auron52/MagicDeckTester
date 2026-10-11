#!/usr/bin/env python3
"""First divergence between two game logs + the PAE-rejected subsets at that turn.
usage: divergence.py <new_dir> <old_dir> [<new_trace_stderr> <old_trace_stderr>]"""
import json, sys, glob, os, re

def load(p):
    fs = sorted(f for f in glob.glob(os.path.join(p, '*.json')))
    return json.load(open(fs[0]))

def acts(g, names):
    out = []
    for t in g['turns']:
        a = []
        for x in t['actions']:
            if x.get('type') == 'UNTAP_SOURCES':
                continue
            s = x.get('type')
            if 'cardName' in x: s += ':' + x['cardName']
            if x.get('type') == 'REVEAL':
                s += ':' + ','.join(str(l.get('cardName')) for l in x.get('lookedAt', []))
            if 'ability' in x: s += '(' + x['ability'] + ')'
            a.append(s)
        out.append((t['turn'], t['phase'], a, t['boardAfter']))
    return out

def names_of(g):
    n = {}
    for k, ids in g['cardNumbering'].items():
        for i in ids: n[i] = k
    return n

def first_solve_casts(path, turn):
    """casting lists of the [edf-prosp] lines inside the FIRST real decision of `turn`
    (between the first [edf-turn] t<turn> line and the next [edf-turn] line)."""
    out = []
    state = 0
    if not os.path.exists(path):
        return None
    for line in open(path, errors='replace'):
        if line.startswith('[edf-turn] t%d ' % turn):
            state += 1
            if state > 1:
                break
            continue
        if state == 1 and line.startswith('[edf-turn]'):
            break
        if state == 1:
            m = re.match(r'\[edf-prosp\] t%d .*casting=\[(.*)\]' % turn, line)
            if m:
                out.append(m.group(1))
    return out

def main():
    nd, od = sys.argv[1], sys.argv[2]
    gn, go = load(nd), load(od)
    nn = names_of(gn)
    an, ao = acts(gn, nn), acts(go, nn)
    print('result new=%s old=%s' % (gn['result'], go['result']))
    print('opening:', [x['cardName'] for x in gn['openingHand']], 'mulls', len(gn['mulliganSequence']) - 1)
    div = None
    for i in range(min(len(an), len(ao))):
        if an[i][2] != ao[i][2]:
            div = i
            break
    if div is None:
        print('no divergence in common prefix'); return
    t, ph = an[div][0], an[div][1]
    prev = an[div - 1][3] if div > 0 else None
    print('DIVERGE at T%d %s' % (t, ph))
    if prev:
        bf = [(x['cardName'] + ('*' if x.get('tapped') else '')) for x in prev['battlefield']]
        print('  board before:', bf)
        print('  hand before :', [nn.get(c, c) for c in prev['hand']], 'opp', prev.get('opponentLife'))
    print('  NEW:', ' | '.join(an[div][2]))
    print('  OLD:', ' | '.join(ao[div][2]))
    for k in range(div + 1, min(div + 4, len(an), len(ao))):
        print('   next NEW T%d %s: %s' % (an[k][0], an[k][1], ' | '.join(an[k][2])[:400]))
        print('   next OLD T%d %s: %s' % (ao[k][0], ao[k][1], ' | '.join(ao[k][2])[:400]))
    if len(sys.argv) > 4:
        cn = first_solve_casts(sys.argv[3], t)
        co = first_solve_casts(sys.argv[4], t)
        if cn is not None and co is not None:
            sn, so = set(cn), set(co)
            print('  subsets scored in first T%d decision: new=%d old=%d' % (t, len(cn), len(co)))
            print('  ONLY OLD (rejected by the fix):', sorted(so - sn))
            print('  ONLY NEW:', sorted(sn - so))

main()
