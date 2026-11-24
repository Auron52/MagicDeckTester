#!/usr/bin/env python3
# usage: report.py <rwf dir> [detail_out]   pairs base / derived / wf per row on game_index; net = summed turn delta over
# games both arms won (+ = slower); lost/gain = wins lost/gained by the second arm. Also cpu ms/game and units/game.
import sys, os, glob, re, json, collections
d = sys.argv[1]
rows = json.load(open(os.path.join(d, "rows.json")))
def load(arm_dir, row, arm):
    r = {}
    for f in glob.glob(f"{d}/{arm_dir}/{row}_{arm}_o*.wins"):
        if not re.fullmatch(rf"{re.escape(row)}_{arm}_o\d+\.wins", os.path.basename(f)): continue
        for ln in open(f):
            a = ln.split()
            if len(a) >= 2: r[int(a[0])] = int(a[1])
    return r
def cost(outf):
    c = collections.defaultdict(lambda: [0, 0, 0])
    if not os.path.exists(outf): return c
    for ln in open(outf):
        m = re.match(r"(\S+)_o\d+: played=(\d+).*units=(\d+) cpu=(\d+)", ln)
        if m: k = m.group(1); c[k][0] += int(m.group(2)); c[k][1] += int(m.group(3)); c[k][2] += int(m.group(4))
    return c
C = cost(f"{d}/base.out"); C.update(cost(f"{d}/fix.out"))
detail = []
print(f"{'row':24s} {'cmp':12s} {'n':>6s} {'chg':>5s} {'fast':>5s} {'slow':>5s} {'lost':>4s} {'gain':>4s} {'net':>5s} {'avgA':>7s} {'avgB':>7s}")
for row in rows:
    W = {"base": load("wins_base", row, "base"), "derived": load("wins_fix", row, "derived"), "wf": load("wins_fix", row, "wf")}
    for A, B in (("base", "derived"), ("base", "wf"), ("derived", "wf")):
        a, b = W[A], W[B]; c = collections.Counter()
        for gi in a:
            if gi not in b: c["missing"] += 1; continue
            x, y = a[gi], b[gi]; c["n"] += 1
            if x > 0: c["wa"] += 1; c["ta"] += x
            if y > 0: c["wb"] += 1; c["tb"] += y
            if x == y: continue
            c["chg"] += 1
            if x > 0 and y > 0:
                c["net"] += y - x
                if y > x: c["slow"] += 1; detail.append((row, A, B, gi, x, y))
                else: c["fast"] += 1
            elif x > 0: c["lost"] += 1; detail.append((row, A, B, gi, x, y))
            else: c["gain"] += 1
        print(f"{row:24s} {A+'>'+B:12s} {c['n']:6d} {c['chg']:5d} {c['fast']:5d} {c['slow']:5d} {c['lost']:4d} {c['gain']:4d} "
              f"{c['net']:+5d} {c['ta']/max(1,c['wa']):7.4f} {c['tb']/max(1,c['wb']):7.4f}" + (f"  MISSING {c['missing']}" if c['missing'] else ""))
    cs = [f"{arm} {C[f'{row}_{arm}'][2]/max(1,C[f'{row}_{arm}'][0]):.0f}ms {C[f'{row}_{arm}'][1]/max(1,C[f'{row}_{arm}'][0]):.0f}u"
          for arm in ("base", "derived", "wf")]
    print(f"{'':24s} cost/game: " + " | ".join(cs))
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(d, "slower.txt")
with open(out, "w") as fo:
    for r in detail: fo.write(" ".join(map(str, r)) + "\n")
