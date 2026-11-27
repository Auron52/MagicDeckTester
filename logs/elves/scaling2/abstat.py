# Paired stats for the scaling2 A/B: per format and arm, avg turn, net vs full and vs holdall, faster/slower, units.
import re, sys, collections
R = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    m = re.match(r'(\w+?)_(\w+)@(\d+): played=1 avg=([\d.]+) digest=(\w+) ms=(\d+) units=(\d+)', line)
    if m: R[(m[1], int(m[3]))][m[2]] = (float(m[4]), int(m[6]), int(m[7]), m[5])
arms = ['holdall', 'lump', 'sink', 'combo', 'full']
for fmt in ('std', '2hg'):
    keys = sorted(k for k in R if k[0] == fmt and all(a in R[k] for a in arms))
    n = len(keys)
    if n == 0: continue
    print(f"== {fmt}: {n} paired games")
    fu = sum(R[k]['full'][2] for k in keys) / n
    for a in arms:
        avg = sum(R[k][a][0] for k in keys) / n
        units = sum(R[k][a][2] for k in keys) / n
        won = sum(1 for k in keys if R[k][a][0] < 9)
        if a == 'full':
            print(f"  {a:8s} avg {avg:.4f} won {won} units/game {units:,.0f}")
            continue
        line = f"  {a:8s} avg {avg:.4f}"
        for ref in ('full', 'holdall'):
            if ref == a: continue
            fa = [k[1] for k in keys if R[k][a][0] < R[k][ref][0]]
            sl = [k[1] for k in keys if R[k][a][0] > R[k][ref][0]]
            dg = sum(1 for k in keys if R[k][a][3] != R[k][ref][3])
            net = avg - sum(R[k][ref][0] for k in keys) / n
            line += f" | vs {ref}: net {net:+.4f} f{len(fa)} s{len(sl)} dig!={dg} slow={sl} fast={fa}"
        print(line + f" | units/game {units:,.0f} x{units/fu:.3f} won {won}")
