#!/usr/bin/env python3
"""Build the recovery game list (deck cell gi seed old new) from the rc3 kit."""
import glob, json, os, re

RC3 = '/workspaces/MagicDeckTester/logs/discard_impl/rc3'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'games.txt')


def turn_of(d):
    fs = sorted(f for f in glob.glob(os.path.join(d, '*.json')) if not f.endswith('census.json'))
    if not fs:
        return None
    g = json.load(open(fs[-1]))
    r = g['result']
    return r.get('turn') if r.get('winner') == 'player' else 9


rows = []
for deck in ('fivecolour', 'stompy', 'selesnya', 'bruna', 'snow'):
    for ln in open(os.path.join(RC3, 'slower_%s.txt' % deck)):
        m = re.match(r'\s*(\S+)\s+(\S+)\s+gi=(\d+)\s+old=(\d+)\s+new=(\d+)\s+seed=(\d+)', ln)
        if m:
            rows.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(6)), int(m.group(4)), int(m.group(5)), 'ab3'))
# FiveColour: the emulation's remaining slower games (Cannons lines + gi123), ab3 seeds.
for gi in (123, 492, 1022, 1405, 1724):
    rows.append(('fivecolour', 'd0', gi, 1210000 + gi + 1,
                 turn_of(os.path.join(RC3, 'fivecolour', 'e%d_old' % gi)),
                 turn_of(os.path.join(RC3, 'fivecolour', 'e%d_newy5' % gi)), 'emu'))
# Stompy held-out slower games (seed base 9300001).
for d in sorted(glob.glob(os.path.join(RC3, 'stompy', 'h*_d0_old'))):
    gi = int(re.search(r'/h(\d+)_d0_old$', d).group(1))
    rows.append(('stompy', 'd0', gi, 9300001 + gi, turn_of(d),
                 turn_of(d.replace('_old', '_new')), 'heldout'))
with open(OUT, 'w') as f:
    for r in rows:
        f.write('%s %s %d %d %s %s %s\n' % r)
print(open(OUT).read())
