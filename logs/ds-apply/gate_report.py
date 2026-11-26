#!/usr/bin/env python3
# gate_report.py <gate_out>: per-arm APPLY-DROP counts of the Dragonstorm gate slice (mk_gate.py + run_gate.sh).
# Flag lines carry seed=30000000+<global gi> job=<arm>__dragonstorm__g<block>; gi= is job-local. Arms off / on cover
# gi 87000-158999 (72k games); the leave-one-out arms cover gi < 105000 (18k), so off / on are also reported there.
# "declared" = the dropped cast is in the plan; "undeclared" = need-later ("not declared"). ev = distinct (seed, turn).
import re, sys
from collections import Counter, defaultdict
out = sys.argv[1]
LOO = 105000
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=(\w+?)__dragonstorm__\S+ gi=-?\d+ (T\d+) m\d plan=')
cnt = defaultdict(Counter); ev = defaultdict(set); gm = defaultdict(set); drop = defaultdict(Counter)
for line in open(out + '/flags.err', errors='ignore'):
    m = pat.match(line)
    if not m:
        continue
    cls, seed, arm, t = m.group(1), int(m.group(2)), m.group(3), m.group(4)
    if cls == 'APPLY-DROP':
        cls = 'AD-undecl' if 'not declared' in line else 'AD-decl'
        d = re.search(r'engine dropped \[cast ([^,\]]*)', line)
        for sl in (('all', 'loo') if seed - 30000000 < LOO else ('all',)):
            ev[(arm, sl, cls)].add((seed, t)); gm[(arm, sl, cls)].add(seed)
            if d and sl == 'all':
                drop[arm][(cls, d.group(1))] += 1
    for sl in (('all', 'loo') if seed - 30000000 < LOO else ('all',)):
        cnt[(arm, sl)][cls] += 1
# per-arm totals from batch.out
tot = defaultdict(lambda: [0, 0.0, 0, 0, 0])
for line in open(out + '/batch.out', errors='ignore'):
    m = re.match(r'(\w+?)__dragonstorm__g(\d+): played=(\d+) avg=([\d.]+) digest=\S+ ms=\d+ units=(\d+) cpu=(\d+)', line)
    if not m:
        continue
    arm, g0, n, avg, u, c = m.group(1), int(m.group(2)), int(m.group(3)), float(m.group(4)), int(m.group(5)), int(m.group(6))
    for sl in (('all', 'loo') if g0 < LOO else ('all',)):
        t = tot[(arm, sl)]; t[0] += n; t[1] += n * avg; t[2] += u; t[3] += c
cls = ['AD-decl', 'AD-undecl', 'APPLY-DROP-ORDER', 'ENUM-GAP', 'ORACLE-GAP', 'CAPPED', 'CAPPED-DECISION']
clsh = ['ADdecl', 'ADundc', 'ADordr', 'ENUM', 'ORGAP', 'CAP', 'CAPdec']
print('%-32s %-4s %7s %8s %8s %6s %6s %6s %6s %6s %6s %6s | %6s %6s | %s' % ('arm', 'gi', 'games', 'turns', 'cpu_s', *clsh, 'ev_d', 'ev_u', 'games_d/u'))
for arm in ['off', 'on', 'no_leftover_swap_wide', 'no_ondemand_visible_lump_exact']:
    for sl in ('all', 'loo'):
        if (arm, sl) not in tot or (sl == 'all' and arm.startswith('no_')):
            continue
        t = tot[(arm, sl)]
        print('%-32s %-4s %7d %8.0f %8d %6d %6d %6d %6d %6d %6d %6d | %6d %6d | %d/%d' % (arm, sl, t[0], t[1], t[3] // 1000,
              *[cnt[(arm, sl)][c] for c in cls], len(ev[(arm, sl, 'AD-decl')]), len(ev[(arm, sl, 'AD-undecl')]),
              len(gm[(arm, sl, 'AD-decl')]), len(gm[(arm, sl, 'AD-undecl')])))
if '--drops' in sys.argv:
    for arm in ('off', 'on'):
        print(arm, ', '.join('%s %s=%d' % (k[0][3:], k[1], v) for k, v in drop[arm].most_common(10)))
