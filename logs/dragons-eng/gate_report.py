#!/usr/bin/env python3
# gate_report.py <gate_out> [<gate_out> ...]: per-run issue counts of the Dragons gate slice (mk_gate.py + run_gate.sh),
# ORACLE-GAP split by family (gy-return plans / a Mind Stone tap difference / other), the oracle summary (decisions,
# plans, applies, ms) and the top APPLY-DROP shapes. Counts are flag LINES (MTG_MANA_ORACLE=2 prints every flag).
import re, sys
from collections import Counter
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=\S+ gi=-?\d+ (T\d+ m\d) plan=(\{[^}]*\})(.*)$')
for out in sys.argv[1:]:
    cls, og, shapes = Counter(), Counter(), Counter()
    summ = []
    for line in open(out + '/flags.err', errors='ignore'):
        if line.startswith('=== MANA ORACLE:') or line.startswith('=== MANA ORACLE classes'):
            summ.append(line.strip())
            continue
        m = pat.match(line)
        if not m:
            continue
        c = m.group(1)
        cls[c] += 1
        if c == 'ORACLE-GAP':
            og['gy-return' if 'gy-return' in m.group(4) else
               ('mind-stone' if re.search(r'Mind Stone#\d+ tapped by', m.group(5)) else 'other')] += 1
        if c == 'APPLY-DROP':
            d = re.search(r'engine dropped (\[[^\]]*\])', m.group(5))
            shapes['%s %s dropped %s' % (m.group(3), re.sub(r'land=[^+}]*', 'land', m.group(4)), d.group(1) if d else '?')] += 1
    print('==', out)
    print('  classes:', dict(sorted(cls.items())))
    print('  ORACLE-GAP by family:', dict(og))
    for s in summ:
        print('  ' + s)
    for k, v in shapes.most_common(8):
        print('  %6d %s' % (v, k))
