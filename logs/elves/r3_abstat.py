# Paired stats for the round-3 A/B: per format and arm, avg win turn, net vs band and vs bandtap (with the paired se),
# faster / slower game lists, digest differences, units/game.
import re, sys, collections, math
R = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    m = re.match(r'(\w+?)_(\w+)@(\d+): played=1 avg=([\d.]+) digest=(\w+) ms=(\d+) units=(\d+)', line)
    if m: R[(m[1], int(m[3]))][m[2]] = (float(m[4]), int(m[6]), int(m[7]), m[5])
arms = ['band', 'bandtap', 'bandtapfix']
for fmt in ('std', '2hg'):
    keys = sorted(k for k in R if k[0] == fmt and all(a in R[k] for a in arms))
    n = len(keys)
    if n == 0: continue
    print(f"== {fmt}: {n} paired games")
    bu = sum(R[k]['band'][2] for k in keys) / n
    for a in arms:
        avg = sum(R[k][a][0] for k in keys) / n
        units = sum(R[k][a][2] for k in keys) / n
        won = sum(1 for k in keys if R[k][a][0] < 9)
        line = f"  {a:8s} avg {avg:.4f} won {won} units/game {units:,.0f} x{units/bu:.4f} (vs band)"
        for ref in ('band', 'bandtap'):
            if ref == a: continue
            d = [R[k][a][0] - R[k][ref][0] for k in keys]
            mu = sum(d) / n
            se = math.sqrt(sum((x - mu) ** 2 for x in d) / (n - 1) / n) if n > 1 else 0
            fa = [k[1] for k in keys if R[k][a][0] < R[k][ref][0]]
            sl = [k[1] for k in keys if R[k][a][0] > R[k][ref][0]]
            dg = sum(1 for k in keys if R[k][a][3] != R[k][ref][3])
            line += f"\n      vs {ref}: net {mu:+.4f} se {se:.4f} faster {len(fa)} slower {len(sl)} digest!= {dg} slow={sl} fast={fa}"
        print(line)
