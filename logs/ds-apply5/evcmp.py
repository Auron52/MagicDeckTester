#!/usr/bin/env python3
# evcmp.py <tagA> <tagB>: per event repro (evrun.sh), the need-later Karrthus APPLY-DROP flags (undeclared, last dropped
# cast Karrthus), all APPLY-DROP flags, the win turn, and whether the two game logs are identical (runId stripped).
import sys, re, os, glob
S = '/workspaces/MagicDeckTester/build/scratch/ds-apply5'
a, b = sys.argv[1], sys.argv[2]
ev = open(S + '/events.txt').read().split()
def stat(tag, s):
    f = S + '/rep_%s_%s' % (tag, s)
    if not os.path.exists(f + '.err'): return None
    k = n = vc = 0
    for l in open(f + '.err', errors='ignore'):
        if l.startswith('[visible-closer]'): vc += 1
        if not l.startswith('[mana-oracle] APPLY-DROP seed'): continue
        n += 1
        d = re.search(r'engine dropped (\[[^\]]*\])', l)
        if d and 'Karrthus' in d.group(1).split(', cast ')[-1] and 'not declared' in l: k += 1
    t = [x for x in open(f + '.out') if x.startswith('avg')]
    return k, n, (t[0].split()[3] if t else '?'), vc
def log(tag, s):
    g = glob.glob(S + '/rep_%s_%s/*.json' % (tag, s))
    return [l for l in open(g[0]) if '"runId"' not in l] if g else None
tot = [0, 0, 0, 0]; same = 0; diff = []
for e in ev:
    s = e.split(':')[0]; x = stat(a, s); y = stat(b, s)
    if x is None or y is None: continue
    tot[0] += x[0]; tot[1] += y[0]; tot[2] += x[1]; tot[3] += y[1]
    la, lb = log(a, s), log(b, s); eq = la is not None and la == lb
    same += eq
    if not eq: diff.append(s)
    print('%s %-3s KarrNL %3d -> %3d  allAD %3d -> %3d  win %s -> %s  vc %d/%d  log %s' % (s, e.split(':')[1], x[0], y[0],
          x[1], y[1], x[2], y[2], x[3], y[3], 'same' if eq else 'DIFF'))
print('total KarrNL %d -> %d, all AD %d -> %d' % tuple(tot), '| logs same %d, differ %s' % (same, diff))
