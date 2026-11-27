#!/usr/bin/env python3
"""Paired tables for the K fleet / SG50 A/B. A loss counts as turn 9. net = slower - faster (negative = new arm better).
Comparisons: O1 vs O0 (K fleet), S0 vs O0 (SG50 alone), S1 vs O0 (SG50+K), S1 vs S0 (K on top of SG50).
units / cpu = new/base per game over the same paired jobs.  Also writes <abdir>/slower.json (every slower game).
usage: summarize.py <abdir>"""
import json, math, os, re, sys
ab = sys.argv[1]
rows = json.load(open(os.path.join(ab, "rows.json")))["rows"]
jl = re.compile(r"^(\S+): played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+) cpu=(\d+)")
J, W = {}, {}
for b in ("O", "S"):
    p = os.path.join(ab, f"batch_{b}.out")
    if os.path.exists(p):
        for ln in open(p):
            m = jl.match(ln)
            if m: J[m.group(1)] = (int(m.group(2)), int(m.group(4)), int(m.group(5)))
    d = os.path.join(ab, f"wins_{b}")
    if os.path.isdir(d):
        for f in os.listdir(d):
            if f.endswith(".wins"):
                W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1])) for p in (l.split() for l in open(os.path.join(d, f))) if len(p) >= 2}
CMP = [("O1", "O0", "K fleet: K=1 vs today"), ("S0", "O0", "SG50 alone vs today"), ("S1", "O0", "SG50+K vs today"), ("S1", "S0", "SG50+K vs SG50")]
slow = []
for new, base, title in CMP:
    out = []
    for r in rows:
        if new not in r["arms"] or base not in r["arms"]: continue
        row = r["row"]; ds = []; tb_all = []; tn_all = []; uB = uN = cB = cN = pB = pN = 0; wb_ = wn_ = 0
        for nb in [n for n in W if n.startswith(f"{row}_{base}_s")]:
            seed = int(nb.split("_s")[-1]); nn = f"{row}_{new}_s{seed}"
            if nn not in W or nb not in J or nn not in J: continue
            for gi, tb in W[nb].items():
                if gi not in W[nn]: continue
                tn = W[nn][gi]; ds.append(tn - tb); tb_all.append(tb); tn_all.append(tn)
                if tn > tb: slow.append(dict(cmp=f"{new}v{base}", row=row, deck=r["deck"], seed=seed, gi=gi, tb=tb, tn=tn, base=base, new=new))
            p, u, c = J[nb]; pB += p; uB += u; cB += c
            p, u, c = J[nn]; pN += p; uN += u; cN += c
        if not ds: continue
        n = len(ds); m = sum(ds) / n
        se = math.sqrt(sum((x - m) ** 2 for x in ds) / max(1, n - 1)) / math.sqrt(n)
        fa, sl = sum(x < 0 for x in ds), sum(x > 0 for x in ds)
        mb = sum(tb_all) / n; mn = sum(tn_all) / n
        out.append(f"| {row} | {n} | {sl - fa:+d} | {fa} / {sl} | {m:+.4f} | {(m / se if se else 0):+.2f} | {mb:.4f} | {mn:.4f} | "
                   f"x{(uN / pN) / (uB / pB):.3f} | x{(cN / pN) / (cB / pB):.3f} |")
    if out:
        print(f"\n### {title} ({new} vs {base})\n")
        print("| row | n | net | faster / slower | d turn | t | mean turn base | mean turn new | units | cpu |")
        print("|---|---|---|---|---|---|---|---|---|---|")
        print("\n".join(out))
json.dump(slow, open(os.path.join(ab, "slower.json"), "w"), indent=1)
print(f"\nslower games: {len(slow)} (written to {ab}/slower.json)")
for c in sorted({s['cmp'] for s in slow}):
    print(f"  {c}: {sum(1 for s in slow if s['cmp'] == c)}")
