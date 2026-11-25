#!/usr/bin/env python3
# ab_report.py <outdir> [--arms off,on] [--list-slower]: the paired play A/B of mk_ab.py (arms off / on). Reads <outdir>/wins.err
# ([win] job=<arm>__<shape>__o<off> gi=<i> wt=<t>) and <outdir>/batch.out (per-job ms / units / cpu). Unwon scores 9
# (paired_wins.py's convention). Per shape: n paired games, net = sum(on - off) in turns (+ = slower), slower / faster
# game counts, paired t, and the on/off ratios of units and cpu.
import re, sys, math
from collections import defaultdict
out = sys.argv[1]
listing = '--list-slower' in sys.argv
A, B = 'off', 'on'          # --arms a,b: compare arm b against arm a (e.g. --arms loose,on)
for i, x in enumerate(sys.argv):
    if x == '--arms':
        A, B = sys.argv[i + 1].split(',')
W = defaultdict(dict)
for line in open(out + '/wins.err', errors='ignore'):
    m = re.match(r'^\[win\] job=(\w+?)__(\w+?)__o\d+ gi=(\d+) wt=(-?\d+)', line)
    if m:
        W[(m.group(1), m.group(2))][int(m.group(3))] = 9 if int(m.group(4)) < 0 else int(m.group(4))
C = defaultdict(lambda: [0, 0, 0])
for line in open(out + '/batch.out', errors='ignore'):
    m = re.match(r'^(\w+?)__(\w+?)__o\d+: played=\d+ .* ms=(\d+) units=(\d+) cpu=(\d+)', line)
    if m:
        c = C[(m.group(1), m.group(2))]
        c[0] += int(m.group(3)); c[1] += int(m.group(4)); c[2] += int(m.group(5))
shapes = sorted({s for (_, s) in W})
tot = [0, 0, 0, 0]
cu = [0, 0, 0, 0]
print('%-22s %6s %6s %5s %5s %7s %8s %8s' % ('shape', 'n', 'net', 'slow', 'fast', 't', 'units', 'cpu'))
slower = []
for s in shapes:
    a, b = W[(A, s)], W[(B, s)]
    ks = sorted(set(a) & set(b))
    d = [b[k] - a[k] for k in ks]
    n = len(d)
    if n == 0:
        continue
    net = sum(d)
    mu = net / n
    var = sum((x - mu) ** 2 for x in d) / (n - 1) if n > 1 else 0
    t = mu / math.sqrt(var / n) if var > 0 else 0.0
    sl = sum(1 for x in d if x > 0)
    fa = sum(1 for x in d if x < 0)
    co, cn = C[(A, s)], C[(B, s)]
    ur = cn[1] / co[1] - 1 if co[1] else 0
    cr = cn[2] / co[2] - 1 if co[2] else 0
    tot[0] += n; tot[1] += net; tot[2] += sl; tot[3] += fa
    cu[0] += co[1]; cu[1] += cn[1]; cu[2] += co[2]; cu[3] += cn[2]
    print('%-22s %6d %+6d %5d %5d %+7.2f %+7.2f%% %+7.2f%%' % (s, n, net, sl, fa, t, 100 * ur, 100 * cr))
    for k in ks:
        if b[k] > a[k]:
            slower.append((s, k, a[k], b[k]))
print('%-22s %6d %+6d %5d %5d %7s %+7.2f%% %+7.2f%%' % ('TOTAL', tot[0], tot[1], tot[2], tot[3], '',
      100 * (cu[1] / cu[0] - 1) if cu[0] else 0, 100 * (cu[3] / cu[2] - 1) if cu[2] else 0))
if listing:
    for s, k, x, y in slower:
        print('SLOWER %s gi=%d off=%d on=%d' % (s, k, x, y))
