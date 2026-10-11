#!/usr/bin/env python3
"""Side-by-side compact per-turn summary of two game logs. usage: side.py <new_dir> <old_dir>"""
import json, sys, glob, os

def load(p):
    fs = sorted(glob.glob(os.path.join(p, '*.json')))
    return json.load(open(fs[0]))

def summ(g):
    names = {}
    for k, ids in g['cardNumbering'].items():
        for i in ids: names[i] = k
    rows = {}
    for t in g['turns']:
        if t['phase'] not in ('MAIN_1', 'MAIN_2', 'COMBAT'):
            continue
        parts = []
        ndraw = 0
        for a in t['actions']:
            ty = a.get('type')
            if ty == 'UNTAP_SOURCES': continue
            if ty == 'DRAW': ndraw += 1; continue
            if ty == 'PLAY_LAND': parts.append('L:' + a['cardName'])
            elif ty == 'CAST_SPELL': parts.append(a['cardName'])
            elif ty == 'REVEAL':
                parts.append('->' + ','.join(str(l.get('cardName')) for l in a.get('lookedAt', [])))
            elif ty == 'ABILITY': parts.append('[' + a.get('ability', '')[:24] + ']')
            elif ty == 'ATTACK': parts.append('atk%s' % a.get('damage'))
            else: parts.append(ty)
        if ndraw: parts.append('draws%d' % ndraw)
        key = (t['turn'], t['phase'])
        rows[key] = ' '.join(parts)
    return rows

def main():
    gn, go = load(sys.argv[1]), load(sys.argv[2])
    rn, ro = summ(gn), summ(go)
    print('NEW result', gn['result'], ' OLD result', go['result'])
    keys = sorted(set(rn) | set(ro), key=lambda k: (k[0], ['MAIN_1', 'COMBAT', 'MAIN_2'].index(k[1])))
    for k in keys:
        a, b = rn.get(k, '-'), ro.get(k, '-')
        if not a and not b: continue
        mark = '  ' if a == b else '**'
        print('%s T%d %-6s NEW: %s' % (mark, k[0], k[1][:6], a[:160]))
        if a != b:
            print('   %-9s OLD: %s' % ('', b[:160]))

main()
