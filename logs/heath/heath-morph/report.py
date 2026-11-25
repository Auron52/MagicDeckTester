#!/usr/bin/env python3
# usage: report.py <dir> [armA armB]  pairs on game_index; net = summed turn delta over both-won games (+ = B slower)
import sys, os, glob, re, json, collections
d = sys.argv[1]; A = sys.argv[2] if len(sys.argv) > 2 else "base"; B = sys.argv[3] if len(sys.argv) > 3 else "new"
rows = json.load(open(os.path.join(d, "rows.json")))
def load(arm, row):
    r = {}
    for f in glob.glob(f"{d}/wins_{arm}/{row}_{arm}_o*.wins"):
        if not re.fullmatch(rf"{re.escape(row)}_{arm}_o\d+\.wins", os.path.basename(f)): continue
        for ln in open(f):
            a = ln.split()
            if len(a) >= 2: r[int(a[0])] = (int(a[1]), a[2] if len(a) > 2 else "")
    return r
def cost(outf):
    c = collections.defaultdict(lambda: [0, 0, 0])
    if os.path.exists(outf):
        for ln in open(outf):
            m = re.match(r"(\S+)_o\d+: played=(\d+).*units=(\d+) cpu=(\d+)", ln)
            if m: k = m.group(1); c[k][0] += int(m.group(2)); c[k][1] += int(m.group(3)); c[k][2] += int(m.group(4))
    return c
C = cost(f"{d}/{A}.out"); C.update(cost(f"{d}/{B}.out"))
detail = []
print(f"{'row':22s} {'n':>6s} {'chg':>5s} {'dig':>5s} {'fast':>5s} {'slow':>5s} {'lost':>4s} {'gain':>4s} {'net':>5s} {'avgA':>7s} {'avgB':>7s}  cpu A/B ms  units A/B")
for row in rows:
    a, b = load(A, row), load(B, row); c = collections.Counter()
    for gi in a:
        if gi not in b: c["missing"] += 1; continue
        (x, da), (y, db) = a[gi], b[gi]; c["n"] += 1
        if da != db: c["dig"] += 1
        if x > 0: c["wa"] += 1; c["ta"] += x
        if y > 0: c["wb"] += 1; c["tb"] += y
        if x == y: continue
        c["chg"] += 1
        if x > 0 and y > 0:
            c["net"] += y - x
            if y > x: c["slow"] += 1; detail.append((row, gi, x, y))
            else: c["fast"] += 1; detail.append((row, gi, x, y, "faster"))
        elif x > 0: c["lost"] += 1; detail.append((row, gi, x, y, "lost"))
        else: c["gain"] += 1; detail.append((row, gi, x, y, "gained"))
    ca, cb = C[f"{row}_{A}"], C[f"{row}_{B}"]
    print(f"{row:22s} {c['n']:6d} {c['chg']:5d} {c['dig']:5d} {c['fast']:5d} {c['slow']:5d} {c['lost']:4d} {c['gain']:4d} {c['net']:+5d} "
          f"{c['ta']/max(1,c['wa']):7.4f} {c['tb']/max(1,c['wb']):7.4f}  {ca[2]/max(1,ca[0]):.0f}/{cb[2]/max(1,cb[0]):.0f}  {ca[1]/max(1,ca[0]):.0f}/{cb[1]/max(1,cb[0]):.0f}"
          + (f"  MISSING {c['missing']}" if c['missing'] else ""))
with open(os.path.join(d, f"changed_{A}_{B}.txt"), "w") as fo:
    for r in detail: fo.write(" ".join(map(str, r)) + "\n")
