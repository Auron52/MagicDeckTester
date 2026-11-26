#!/usr/bin/env python3
"""Paired tables for the Mill / K boundary-fix A/B. A loss counts as turn 9. net = slower - faster (negative = new better).
Comparisons: K vs T, KF vs T, KF vs K. units / cpu = new/base per game over the paired jobs. 'ident' = jobs whose digest
AND units are equal in both arms (KF vs K: byte-identical search). Writes <abdir>/slower.json (every slower game).
usage: summarize.py <abdir>"""
import json, math, os, re, sys
ab = sys.argv[1]
rows = json.load(open(os.path.join(ab, "rows.json")))["rows"]
jl = re.compile(r"^(\S+): played=(\d+) avg=\S+ digest=(\S+) ms=(\d+) units=(\d+) cpu=(\d+)")
J, W = {}, {}
for ln in open(os.path.join(ab, "batch.out")):
    m = jl.match(ln)
    if m: J[m.group(1)] = (int(m.group(2)), m.group(3), int(m.group(5)), int(m.group(6)))
d = os.path.join(ab, "wins")
for f in os.listdir(d):
    if f.endswith(".wins"):
        W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1])) for p in (l.split() for l in open(os.path.join(d, f))) if len(p) >= 2}
CMP = [("K", "T", "K vs today"), ("KF", "T", "K+fix vs today"), ("KF", "K", "K+fix vs K")]
slow = []; tot = {}
for new, base, title in CMP:
    out = []
    for r in rows:
        if new not in r["arms"] or base not in r["arms"]: continue
        row = r["row"]; ds = []; tb_all = []; tn_all = []; uB = uN = cB = cN = pB = pN = 0; ident = nj = 0
        for nb in sorted(n for n in W if n.startswith(f"{row}_{base}_s")):
            seed = int(nb.split("_s")[-1]); nn = f"{row}_{new}_s{seed}"
            if nn not in W or nb not in J or nn not in J: continue
            for gi, tb in W[nb].items():
                if gi not in W[nn]: continue
                tn = W[nn][gi]; ds.append(tn - tb); tb_all.append(tb); tn_all.append(tn)
                if tn > tb: slow.append(dict(cmp=f"{new}v{base}", row=row, deck=r["deck"], seed=seed, gi=gi, tb=tb, tn=tn, base=base, new=new))
            p, dg, u, c = J[nb]; pB += p; uB += u; cB += c; db, ub = dg, u
            p, dg, u, c = J[nn]; pN += p; uN += u; cN += c
            nj += 1; ident += (dg == db and u == ub)
        if not ds: continue
        n = len(ds); m = sum(ds) / n
        se = math.sqrt(sum((x - m) ** 2 for x in ds) / max(1, n - 1)) / math.sqrt(n)
        fa, sl = sum(x < 0 for x in ds), sum(x > 0 for x in ds)
        t = tot.setdefault(title, [0, 0, 0]); t[0] += sl - fa; t[1] += fa; t[2] += sl
        out.append(f"| {row} | {n} | {sl - fa:+d} | {fa} / {sl} | {m:+.4f} | {(m / se if se else 0):+.2f} | {sum(tb_all) / n:.4f} | "
                   f"{sum(tn_all) / n:.4f} | x{(uN / pN) / (uB / pB):.3f} | x{(cN / pN) / (cB / pB):.3f} | {ident}/{nj} |")
    if out:
        print(f"\n### {title} ({new} vs {base})\n")
        print("| row | n | net | faster / slower | d turn | t | mean turn base | mean turn new | units | cpu | ident jobs |")
        print("|---|---|---|---|---|---|---|---|---|---|---|")
        print("\n".join(out))
        print(f"\ntotal net {tot[title][0]:+d} ({tot[title][1]} faster / {tot[title][2]} slower)")
json.dump(slow, open(os.path.join(ab, "slower.json"), "w"), indent=1)
print(f"\nslower games: {len(slow)} (written to {ab}/slower.json)")
for c in sorted({s['cmp'] for s in slow}):
    print(f"  {c}: {sum(1 for s in slow if s['cmp'] == c)}")
