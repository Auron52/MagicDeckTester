#!/usr/bin/env python3
# attrib.py <gate_out>: (ds-apply5 copy of logs/ds-apply4/attrib.py; arms base / on, the KarNL column = need-later Karrthus)
# undeclared, keyed by the LAST dropped cast (round-3 / minirep classification), plus CAPPED-DECISION and the per-arm
# units/cpu from batch.out. Also the CAPPED-DECISION game set diff off vs on.
import re, sys
from collections import Counter, defaultdict
out = sys.argv[1]; LOO = 105000
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=(\w+?)__dragonstorm__\S+ gi=-?\d+ (T\d+) m\d plan=')
cnt = defaultdict(Counter); drop = defaultdict(Counter); ev = defaultdict(set); capg = defaultdict(Counter)
for line in open(out + '/flags.err', errors='ignore'):
    m = pat.match(line)
    if not m: continue
    cls, seed, arm, t = m.group(1), int(m.group(2)), m.group(3), m.group(4)
    sls = ('all', 'loo') if seed - 30000000 < LOO else ('all',)
    if cls == 'APPLY-DROP':
        cls = 'ADundc' if 'not declared' in line else 'ADdecl'
        d = re.search(r'engine dropped (\[[^\]]*\])', line)
        last = d.group(1).split(', cast ')[-1].strip('[]').replace('cast ', '') if d else '?'
        for sl in sls:
            drop[(arm, sl)][(cls, last)] += 1; ev[(arm, sl, cls)].add((seed, t))
    if cls == 'CAPPED-DECISION':
        capg[arm][(seed, t)] += 1
    for sl in sls: cnt[(arm, sl)][cls] += 1
tot = defaultdict(lambda: [0, 0.0, 0, 0])
for line in open(out + '/batch.out', errors='ignore'):
    m = re.match(r'(\w+?)__dragonstorm__g(\d+): played=(\d+) avg=([\d.]+) digest=\S+ ms=\d+ units=(\d+) cpu=(\d+)', line)
    if not m: continue
    arm, g0, n, avg, u, c = m.group(1), int(m.group(2)), int(m.group(3)), float(m.group(4)), int(m.group(5)), int(m.group(6))
    for sl in (('all', 'loo') if g0 < LOO else ('all',)):
        t = tot[(arm, sl)]; t[0] += n; t[1] += n * avg; t[2] += u; t[3] += c
arms = ['base', 'on']
print('%-28s %-3s %6s %8s %11s %7s %6s %6s %6s %6s %6s %6s %5s %5s' % ('arm', 'sl', 'games', 'turns', 'units', 'cpu_s',
      'ADdecl', 'ADundc', 'ADordr', 'ENUM', 'CAPdec', 'KarNL', 'evD', 'evU'))
for sl in ('all', 'loo'):
    for arm in arms:
        if (arm, sl) not in tot: continue
        t = tot[(arm, sl)]; c = cnt[(arm, sl)]
        kold = drop[(arm, sl)][('ADundc', 'Karrthus, Tyrant of Jund')]
        print('%-28s %-3s %6d %8.0f %11d %7d %6d %6d %6d %6d %6d %6d %5d %5d' % (arm, sl, t[0], t[1], t[2], t[3] // 1000,
              c['ADdecl'], c['ADundc'], c['APPLY-DROP-ORDER'], c['ENUM-GAP'], c['CAPPED-DECISION'], kold,
              len(ev[(arm, sl, 'ADdecl')]), len(ev[(arm, sl, 'ADundc')])))
print()
for sl in ('all', 'loo'):
    for arm in arms:
        if (arm, sl) not in tot: continue
        print(arm, sl, ', '.join('%s %s=%d' % (k[0], k[1], v) for k, v in drop[(arm, sl)].most_common(8)))
print()
a, b = capg['base'], capg['on']
print('CAPPED-DECISION (seed,turn) events: off %d on %d; only-off %d only-on %d; flags in shared events off %d on %d' % (
      len(a), len(b), len(set(a) - set(b)), len(set(b) - set(a)),
      sum(a[k] for k in set(a) & set(b)), sum(b[k] for k in set(a) & set(b))))
for k in sorted(set(b) - set(a)): print('  only-on', k, b[k])
for k in sorted(set(a) - set(b)): print('  only-off', k, a[k])
for k in sorted(set(a) & set(b)):
    if a[k] != b[k]: print('  shared', k, a[k], '->', b[k])
