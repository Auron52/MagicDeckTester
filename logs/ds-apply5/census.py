#!/usr/bin/env python3
# census.py <flags.err> <arm>: the need-later (undeclared) APPLY-DROP family, keyed by the LAST dropped cast; per
# (seed, turn) event: flag count, distinct plans, dropped lists, board/hand, rollback reasons.
import re, sys
from collections import defaultdict, Counter
fn, arm = sys.argv[1], sys.argv[2]
card = sys.argv[3] if len(sys.argv) > 3 else 'Karrthus, Tyrant of Jund'
pat = re.compile(r'^\[mana-oracle\] APPLY-DROP seed=(\d+) job=(\w+?)__dragonstorm__\S+ gi=-?\d+ (T\d+) m(\d) plan=\{(.*?)\} \| engine dropped (\[[^\]]*\])')
ev = defaultdict(list)
for line in open(fn, errors='ignore'):
    if not line.startswith('[mana-oracle] APPLY-DROP seed'): continue
    m = pat.match(line)
    if not m or m.group(2) != arm or 'not declared' not in line: continue
    last = m.group(6).split(', cast ')[-1].strip('[]').replace('cast ', '')
    if last != card: continue
    ev[(int(m.group(1)), m.group(3))].append((m.group(4), m.group(5), m.group(6), line))
tot = 0
shapes = Counter(); shapeflags = Counter()
for (s, t), L in sorted(ev.items()):
    tot += len(L)
    plans = Counter(p for _, p, _, _ in L)
    line = L[0][3]
    b = re.search(r'\| board=\[(.*?)\] hand=\[(.*?)\] float=(\S+)', line)
    board, hand = (b.group(1), b.group(2)) if b else ('?', '?')
    napex = max(p.count('Apex of Power') for p in plans)
    terr = 'Territory' in board or 'Unclaimed Territory' in ' '.join(plans)
    bloom = 'Lotus Bloom' in board
    rb = Counter(re.search(r'\|\| ROLLBACK (\w+)', l).group(1) if re.search(r'\|\| ROLLBACK (\w+)', l) else '-' for _, _, _, l in L)
    sh = 'apex%d%s%s' % (napex, '+terr' if terr else '', '+bloom' if bloom else '')
    shapes[sh] += 1; shapeflags[sh] += len(L)
    print('%d %s flags=%d plans=%d rb=%s shape=%s' % (s, t, len(L), len(plans), dict(rb), sh))
    for p, c in plans.most_common(3): print('    %3d plan={%s}' % (c, p))
    print('    board=[%s] hand=[%s]' % (board, hand))
print('total flags', tot, 'events', len(ev))
for k, v in shapes.most_common(): print('  shape %-20s events %3d flags %4d' % (k, v, shapeflags[k]))
