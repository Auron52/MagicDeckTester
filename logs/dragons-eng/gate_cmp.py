#!/usr/bin/env python3
# gate_cmp.py <gate_base> <gate_new>: the dragons-eng targeted signatures, base (both levers off) vs new (on).
#   dig-float : APPLY-DROP whose oracle payment keeps a float colour off a colourless source (item 1's shape:
#               "pay1[<src>:C keep-floating {R}] ; pay2[(float only)]")
#   rebuy     : APPLY-DROP of the Haven's own activation ("act Haven of the Spirit Dragon unpaid") -- item 3's shape
# plus the per-process class summary (PHANTOM is quiet: summary only) and the APPLY-DROP seed/turn sets that moved.
import re, sys
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=\S+ gi=-?\d+ (T\d+ m\d) plan=(\{[^}]*\})(.*)$')
def load(d):
    cls, sig, keys, summ = {}, {'dig-float': 0, 'rebuy': 0, 'other': 0}, set(), ''
    for line in open(d + '/flags.err', errors='ignore'):
        if line.startswith('=== MANA ORACLE classes'):
            summ = line.strip()
        m = pat.match(line)
        if not m or m.group(1) != 'APPLY-DROP':
            continue
        rest = m.group(5)
        keys.add((int(m.group(2)), m.group(3)))
        if re.search(r'engine dropped \[act Haven of the Spirit Dragon', rest):
            sig['rebuy'] += 1
        elif re.search(r'every action pays with pay1\[[^\]]*:C keep-floating \{[WUBRG]+\}\] ; pay2\[\(float only\)\]', rest):
            sig['dig-float'] += 1
        else:
            sig['other'] += 1
    return summ, sig, keys
b, n = load(sys.argv[1]), load(sys.argv[2])
print('base:', b[0]); print('new: ', n[0])
for k in ('dig-float', 'rebuy', 'other'):
    print('  APPLY-DROP %-10s base %6d  new %6d' % (k, b[1][k], n[1][k]))
print('  APPLY-DROP (seed,turn) decisions: base %d new %d; gone %d, new %d' %
      (len(b[2]), len(n[2]), len(b[2] - n[2]), len(n[2] - b[2])))
