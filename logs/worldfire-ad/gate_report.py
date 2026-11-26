#!/usr/bin/env python3
# gate_report.py <gate_out>: per-arm issue counts of the Worldfire gate slice (mk_gate.py + run_gate.sh). Flag lines
# carry seed=30000000+<global gi> job=<arm>__worldfire__g<block> (their gi= is job-local). Arms off / on cover gi 0-22999; the leave-one-out and loose arms cover
# gi < 6000, so off / on are also reported on that slice. --shapes prints the top APPLY-DROP plan shapes for an arm.
import re, sys
from collections import Counter, defaultdict
out = sys.argv[1]
shapes_arm = sys.argv[sys.argv.index('--shapes') + 1] if '--shapes' in sys.argv else None
cnt = defaultdict(Counter)        # (arm, slice) -> class -> n
shp = Counter()
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=(\w+?)__worldfire__\S+ gi=-?\d+ (T\d+ m\d) plan=(\{[^}]*\})'
                 r'(?: \| engine dropped (\[[^\]]*\]))?')
for line in open(out + '/flags.err', errors='ignore'):
    m = pat.match(line)
    if not m:
        continue
    cls, arm, gi = m.group(1), m.group(3), int(m.group(2)) - 30000000   # the flag's gi= is job-local
    cnt[(arm, 'all')][cls] += 1
    if gi < 6000:
        cnt[(arm, '6k')][cls] += 1
    if shapes_arm == arm and cls == 'APPLY-DROP':
        plan = re.sub(r'land=[^+}]*', 'land', m.group(5))
        shp['%s %s dropped %s' % (m.group(4), plan, m.group(6))] += 1
classes = sorted({c for v in cnt.values() for c in v})
print('%-10s %-4s %s' % ('arm', 'gi', ' '.join('%12s' % c[:12] for c in classes)))
for arm in ['off', 'on', 'nomulti', 'nosuspend', 'noritual', 'loose']:
    for sl in ('all', '6k'):
        if (arm, sl) in cnt and not (sl == 'all' and arm not in ('off', 'on')):
            print('%-10s %-4s %s' % (arm, sl, ' '.join('%12d' % cnt[(arm, sl)][c] for c in classes)))
if shapes_arm:
    for k, v in shp.most_common(15):
        print('%6d %s' % (v, k))
