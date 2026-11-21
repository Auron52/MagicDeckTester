#!/usr/bin/env python3
# Compare the paired A/B: per deck avg win turn (unwon = 9), faster/slower games, units per arm.
import glob, os, re, sys, collections
S = sys.argv[1]
res = collections.defaultdict(dict)   # (deck, arm) -> gi -> (turn, digest)
for f in glob.glob(os.path.join(S, 'games', '*.wins')):
    m = re.match(r'(pd2hg|pd)_(on|off)_(\d+)\.wins', os.path.basename(f))
    if not m: continue
    for line in open(f):
        p = line.split()
        if len(p) >= 3: res[(m[1], m[2])][int(p[0])] = (int(p[1]), p[2])
units = collections.Counter()
for line in open(os.path.join(S, 'batch.out')):
    m = re.match(r'(pd2hg|pd)_(on|off)_\d+: .*units=(\d+)', line)
    if m: units[(m[1], m[2])] += int(m[3])
sc = lambda t: 9 if t < 0 else t
for deck in ('pd', 'pd2hg'):
    a, b = res[(deck, 'off')], res[(deck, 'on')]
    g = sorted(set(a) & set(b))
    if not g: continue
    net = sum(sc(b[i][0]) - sc(a[i][0]) for i in g)
    slower = [(i, a[i][0], b[i][0]) for i in g if sc(b[i][0]) > sc(a[i][0])]
    faster = [(i, a[i][0], b[i][0]) for i in g if sc(b[i][0]) < sc(a[i][0])]
    changed = sum(1 for i in g if a[i][1] != b[i][1])
    print(f"{deck}: pairs={len(g)} off_avg={sum(sc(a[i][0]) for i in g)/len(g):.4f} on_avg={sum(sc(b[i][0]) for i in g)/len(g):.4f} "
          f"net={net:+d} faster={len(faster)} slower={len(slower)} digests_changed={changed} "
          f"units off={units[(deck,'off')]} on={units[(deck,'on')]} ({(units[(deck,'on')]/max(1,units[(deck,'off')])-1)*100:+.2f}%)")
    print("  slower (gi off->on):", slower)
    print("  faster (gi off->on):", faster)
