#!/usr/bin/env python3
"""qvt.py <qvt manifest> <batch stdout(s), comma-separated> <wins dir> [--base off --arm on]: QUALITY VS TIME per deck.
For each deck key (and ALL), on the games every arm played (loss = 9; .wins writes a no-win as <= 0):
  net turns of each arm vs the base (sum over games; negative = faster), its units and cpu ratio vs the base,
  and the EQUAL-COST comparison: the base's own curve (base and every offxNN arm, points (cost ratio, net turns))
  linearly interpolated at the change arm's cost ratio; `@u` on units, `@cpu` on thread-cpu (the wall proxy).
  matched = net(change) - base_curve(cost(change)); <= 0 means the change is at least as good as the base would be
  given the same extra cost. Outside the curve's range it extrapolates on the last segment (marked '*')."""
import json, re, os, sys, glob, collections
man, outs, W = sys.argv[1], sys.argv[2].split(','), sys.argv[3]
base = sys.argv[sys.argv.index('--base') + 1] if '--base' in sys.argv else 'off'
chg = sys.argv[sys.argv.index('--arm') + 1] if '--arm' in sys.argv else 'on'
M = {j['name']: j for j in json.load(open(man))['jobs']}
arms = sorted({n.split('__', 1)[0] for n in M})
curve = [base] + sorted(a for a in arms if a.startswith(base + 'x'))
cost = {}
for f in outs:
    for l in open(f):
        m = re.match(r'(\S+): played=(\d+) avg=\S+ digest=\w+ ms=(\d+) units=(\d+) cpu=(\d+)', l)
        if m: cost[m[1]] = (int(m[2]), int(m[3]), int(m[4]), int(m[5]))
def rd(p):
    r = {}
    for l in open(p):
        a = l.split(); w = int(a[1]); r[int(a[0])] = 9 if w <= 0 else w
    return r
def deck(rest): return re.sub(r'_(overnight|heldout|smoke|regression)_.*', '', rest)
rests = sorted({n.split('__', 1)[1] for n in M})
T = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0, 0, 0]))   # deck -> arm -> [turns, units, cpu, games]
miss = 0
for rest in rests:
    ws = {}
    for a in arms:
        p = f"{W}/{a}__{rest}.wins"
        if not os.path.exists(p) or f"{a}__{rest}" not in cost: ws = None; break
        ws[a] = rd(p)
    if ws is None: miss += 1; continue
    common = set.intersection(*(set(v) for v in ws.values()))
    full = all(len(v) == len(common) for v in ws.values())
    for a in arms:
        played, ms, u, c = cost[f"{a}__{rest}"]
        for k in (deck(rest), 'ALL'):
            t = T[k][a]; t[0] += sum(ws[a][g] for g in common); t[3] += len(common)
            if full: t[1] += u; t[2] += c
            else: t[1] += u * len(common) / max(1, played); t[2] += c * len(common) / max(1, played)
def interp(pts, x):
    pts = sorted(pts)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if x0 <= x <= x1: return y0 + (y1 - y0) * (x - x0) / (x1 - x0) if x1 > x0 else y0, ''
    (x0, y0), (x1, y1) = (pts[0], pts[1]) if x < pts[0][0] else (pts[-2], pts[-1])
    return (y0 + (y1 - y0) * (x - x0) / (x1 - x0) if x1 != x0 else y0), '*'
print(f"jobs missing an arm: {miss}; curve arms: {curve}")
hdr = f"{'deck':16} {'games':>6} | {chg+' net':>8} {'u':>6} {'cpu':>6} | " + ' | '.join(f"{a} net/u/cpu" for a in curve[1:]) + " | matched@u matched@cpu"
print(hdr)
for k in sorted(T, key=lambda k: (k != 'ALL', k)):
    b = T[k][base]
    if b[3] == 0: continue
    row = lambda a: (T[k][a][0] - b[0], T[k][a][1] / max(1, b[1]), T[k][a][2] / max(1, b[2]))
    n, u, c = row(chg)
    pu = [(1.0, 0.0)] + [(row(a)[1], row(a)[0]) for a in curve[1:]]
    pc = [(1.0, 0.0)] + [(row(a)[2], row(a)[0]) for a in curve[1:]]
    mu, fu = interp(pu, u); mc, fc = interp(pc, c)
    curv = ' | '.join(f"{row(a)[0]:+5d}/{row(a)[1]:.3f}/{row(a)[2]:.3f}" for a in curve[1:])
    print(f"{k:16} {b[3]:6d} | {n:+8d} {u:6.3f} {c:6.3f} | {curv} | {n - mu:+8.1f}{fu:1} {n - mc:+8.1f}{fc:1}")
