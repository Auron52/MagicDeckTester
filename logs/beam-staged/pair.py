#!/usr/bin/env python3
"""Paired delta of arm X vs arm Y per row (a loss counts as 9): d, se, t, faster/slower.  usage: pair.py <abdir> X Y"""
import json, math, os, sys
out, X, Y = sys.argv[1], sys.argv[2], sys.argv[3]
rows = json.load(open(os.path.join(out, "rows.json")))
W = {}
for f in os.listdir(os.path.join(out, "wins")):
    if f.endswith(".wins"):
        W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1]))
                     for p in (l.split() for l in open(os.path.join(out, "wins", f))) if len(p) >= 2}
def g(row, arm):
    r = {}
    for n, w in W.items():
        if n.startswith(f"{row}_{arm}_s"):
            s = int(n[len(f"{row}_{arm}_s"):])
            for gi, t in w.items():
                r[(s, gi)] = t
    return r
for r in rows["rows"]:
    eq = rows["equiv"][r["row"]]
    if X not in eq or Y not in eq:
        continue
    a, b = g(r["row"], eq[X]), g(r["row"], eq[Y])
    ks = sorted(set(a) & set(b))
    ds = [a[k] - b[k] for k in ks]
    n = len(ds); m = sum(ds) / n
    sd = math.sqrt(sum((x - m) ** 2 for x in ds) / max(1, n - 1)); se = sd / math.sqrt(n)
    print(f"{r['row']:26s} {X} vs {Y}: n={n} d={m:+.4f} se={se:.4f} t={(m / se if se else 0):+.2f} "
          f"faster={sum(x < 0 for x in ds)} slower={sum(x > 0 for x in ds)}")
