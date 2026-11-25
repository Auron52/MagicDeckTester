#!/usr/bin/env python3
"""Task 6 A/B table: per row, F vs B paired per game (a loss counts as 9). net = slower - faster (negative = F better);
units / cpu = F/B per game over the same jobs. Also lists every slower game (row seed gi B F) and checks the repro_
jobs against the beam-staged report's SG50 wins (per-game turn AND decision digest).
usage: summarize.py <abdir> [<beam-staged abdir>]"""
import json, math, os, re, sys
out = sys.argv[1]
rows = json.load(open(os.path.join(out, "rows.json")))["rows"]
jl = re.compile(r"^(\S+): played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+) cpu=(\d+)")
def jobs(arm):
    J = {}
    for ln in open(os.path.join(out, f"batch_{arm}.out")):
        m = jl.match(ln)
        if m:
            J[m.group(1)] = (int(m.group(2)), int(m.group(4)), int(m.group(5)))
    return J
def wins(d):
    W = {}
    for f in os.listdir(d):
        if f.endswith(".wins"):
            W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1]), p[2] if len(p) > 2 else "")
                         for p in (l.split() for l in open(os.path.join(d, f))) if len(p) >= 2}
    return W
JB, JF = jobs("B"), jobs("F")
WB, WF = wins(os.path.join(out, "wins_B")), wins(os.path.join(out, "wins_F"))
print("| row | n | net (slower-faster) | faster / slower | d turns | se | t | units F/B | cpu F/B |")
print("|---|---|---|---|---|---|---|---|---|")
slow = []
for r in rows:
    row = r["row"]; ds = []; uB = uF = cB = cF = pB = pF = 0
    for nb, wb in WB.items():
        if not nb.startswith(f"{row}_B_s"):
            continue
        seed = int(nb.split("_s")[-1]); nf = f"{row}_F_s{seed}"
        if nf not in WF:
            continue
        for gi, (tb, _) in wb.items():
            if gi in WF[nf]:
                tf = WF[nf][gi][0]; ds.append(tf - tb)
                if tf > tb:
                    slow.append((row, seed, gi, tb, tf))
        p, u, c = JB[nb]; pB += p; uB += u; cB += c
        p, u, c = JF[nf]; pF += p; uF += u; cF += c
    if not ds:
        continue
    n = len(ds); m = sum(ds) / n
    se = math.sqrt(sum((x - m) ** 2 for x in ds) / max(1, n - 1)) / math.sqrt(n)
    fa, sl = sum(x < 0 for x in ds), sum(x > 0 for x in ds)
    print(f"| {row} | {n} | {sl - fa:+d} | {fa} / {sl} | {m:+.4f} | {se:.4f} | {(m / se if se else 0):+.2f} | "
          f"x{(uF / pF) / (uB / pB):.3f} | x{(cF / pF) / (cB / pB):.3f} |")
print(f"\nslower games ({len(slow)}):")
for s in slow:
    print("  %s seed %d gi %d: B %d -> F %d" % s)
if len(sys.argv) > 2:
    WR = wins(os.path.join(sys.argv[2], "wins"))
    for n, w in WF.items():
        if n.startswith("repro_"):
            ref = WR.get(n[len("repro_"):], {})
            same_t = sum(1 for gi in w if gi in ref and w[gi][0] == ref[gi][0])
            same_d = sum(1 for gi in w if gi in ref and w[gi] == ref[gi])
            print(f"{n}: {len(w)} games, same turn {same_t}, same turn+digest {same_d} (vs report SG50)")
