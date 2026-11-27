#!/usr/bin/env python3
# gate_report.py <gate_out> [--shapes ARM] [--undeclared]: per-arm issue counts of the Treasure Hunt gate slice
# (mk_gate.py + run_gate.sh; from logs/worldfire-ad/gate_report.py). --shapes prints the top APPLY-DROP shapes of ARM,
# split by whether the dropped action was declared in the plan.
import re, sys
from collections import Counter, defaultdict
out = sys.argv[1]
shapes_arm = sys.argv[sys.argv.index('--shapes') + 1] if '--shapes' in sys.argv else None
cnt = defaultdict(Counter)
shp = Counter()
und = Counter()
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=(\w+?)__treasure_hunt__\S+ gi=-?\d+ (T\d+ m\d) plan=(\{[^}]*\})'
                 r'(?: \| engine dropped (\[[^\]]*\]))?')
for line in open(out + '/flags.err', errors='ignore'):
    m = pat.match(line)
    if not m:
        continue
    cls, arm = m.group(1), m.group(3)
    cnt[arm][cls] += 1
    if cls == 'APPLY-DROP':
        u = 'not declared' in line
        cnt[arm]['AD-undecl'] += u
        if shapes_arm == arm:
            plan = re.sub(r'land=[^+}]*', 'land', m.group(5))
            shp['%s %s %s dropped %s' % ('U' if u else 'D', m.group(4), plan, m.group(6))] += 1
classes = ['APPLY-DROP', 'AD-undecl', 'APPLY-DROP-ORDER', 'ENUM-GAP', 'ORACLE-GAP', 'CAPPED', 'CAPPED-DECISION']
print('%-8s %s' % ('arm', ' '.join('%16s' % c for c in classes)))
for arm in sorted(cnt):
    print('%-8s %s' % (arm, ' '.join('%16d' % cnt[arm][c] for c in classes)))
if shapes_arm:
    for k, v in shp.most_common(25):
        print('%6d %s' % (v, k))
