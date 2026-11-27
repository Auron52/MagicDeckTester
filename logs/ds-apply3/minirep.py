#!/usr/bin/env python3
# minirep.py <dir>...: per arm, issue counts by class, APPLY-DROP flags + unique (seed, turn) events by last dropped card.
import re, sys
from collections import Counter
for d in sys.argv[1:]:
    cls = Counter(); ev = {}; fl = Counter()
    for l in open(d + '/flags.err', errors='ignore'):
        m = re.match(r'^\[mana-oracle\] (\S+) seed=(\d+) \S+ \S+ (T\d+) m\d plan=\{[^}]*\}(?: \| engine dropped (\[[^\]]*\]))?', l)
        if not m: continue
        cls[m.group(1)] += 1
        if m.group(1) == 'APPLY-DROP':
            dr = m.group(4).split(', cast ')[-1].strip('[]').replace('cast ', '')
            nl = 'NL' if 'not declared' in l else 'D'
            fl[(dr, nl)] += 1
            ev.setdefault((m.group(2), m.group(3)), (dr, nl))
    evc = Counter(ev.values())
    print(d.rstrip('/').split('/')[-1], dict(cls), 'events', len(ev))
    for k, v in fl.most_common(8): print('   %4d flags %3d events %s' % (v, evc[k], k))
