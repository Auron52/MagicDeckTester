#!/usr/bin/env python3
"""Markdown tables for the beam-staged A/B (pairs every arm vs A per game; a loss counts as 9).
usage: summarize.py <abdir> [--partial]
Per row and arm: net = slower - faster games, d = avg-turn delta vs A, cpu = thread-cpu ratio vs A (per game).
"""
import json, os, re, sys

out = sys.argv[1]
rows = json.load(open(os.path.join(out, "rows.json")))
jl = re.compile(r"^(\S+): played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+) cpu=(\d+)")
jobs = {}
for ln in open(os.path.join(out, "batch.out")):
    m = jl.match(ln)
    if m:
        jobs[m.group(1)] = (int(m.group(2)), int(m.group(4)), int(m.group(5)))
W = {}
for f in os.listdir(os.path.join(out, "wins")):
    if f.endswith(".wins") and f[:-5] in jobs:
        W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1]))
                     for p in (ln.split() for ln in open(os.path.join(out, "wins", f))) if len(p) >= 2}


def arm_data(row, arm):
    res, pl, cpu, un = {}, 0, 0, 0
    pre = f"{row}_{arm}_s"
    for name, w in W.items():
        if name.startswith(pre):
            seed = int(name[len(pre):])
            for gi, t in w.items():
                res[(seed, gi)] = t
            p, u, c = jobs[name]; pl += p; cpu += c; un += u
    return res, pl, cpu, un


table = {}
for r in rows["rows"]:
    row, eq = r["row"], rows["equiv"][r["row"]]
    base, bp, bc, bu = arm_data(row, eq["A"])
    for arm in eq:
        res, pl, cpu, un = arm_data(row, eq[arm])
        keys = sorted(set(base) & set(res))
        if not keys:
            continue
        ds = [res[k] - base[k] for k in keys]
        n = len(ds)
        # cpu per game, both arms over the SAME paired keys' jobs (whole jobs -- complete jobs only)
        table[(row, arm)] = dict(n=n, d=sum(ds) / n, fa=sum(x < 0 for x in ds), sl=sum(x > 0 for x in ds),
                                 cpu=(cpu / pl) / (bc / bp) if pl and bp and bc else float("nan"),
                                 ms=cpu / pl if pl else float("nan"), un=(un / pl) / (bu / bp) if pl and bu else 0,
                                 same=(eq[arm] == eq["A"]))
arms = []
for r in rows["rows"]:
    for a in rows["equiv"][r["row"]]:
        if a not in arms:
            arms.append(a)
for r in rows["rows"]:
    row = r["row"]
    print(f"\n### {row} (n/arm {r['games']})\n")
    print("| arm | n | net (sl-fa) | faster/slower | d_vs_A | cpu vs A | units vs A | ms/game |")
    print("|---|---|---|---|---|---|---|---|")
    for a in arms:
        t = table.get((row, a))
        if not t:
            continue
        tag = " (== A)" if t["same"] and a != "A" else ""
        print(f"| {a}{tag} | {t['n']} | {t['sl'] - t['fa']:+d} | {t['fa']}/{t['sl']} | {t['d']:+.4f} | x{t['cpu']:.2f} | "
              f"x{t['un']:.2f} | {t['ms']:.0f} |")
# Sums: per arm over rows of a group (play d5 std; 2HG; d3), net and cpu-weighted ratio
def group(pred, label):
    print(f"\n### SUM {label}\n")
    print("| arm | rows | net | sum d (turns x games / n) | cpu vs A (sum) |")
    print("|---|---|---|---|---|")
    for a in arms:
        net, rs, dsum, ca, cb = 0, 0, 0.0, 0.0, 0.0
        for r in rows["rows"]:
            if not pred(r):
                continue
            t = table.get((r["row"], a)); b = table.get((r["row"], "A"))
            if not t or not b:
                continue
            rs += 1; net += t["sl"] - t["fa"]; dsum += t["d"]
            ca += t["ms"] * t["n"]; cb += b["ms"] * t["n"]
        if rs:
            print(f"| {a} | {rs} | {net:+d} | {dsum:+.4f} | x{ca / cb:.3f} |")
core = {"creature_giving", "antilife", "dragonstorm"}
group(lambda r: r["deck"] in core and r["depth"] == 5 and not r["hg"], "CG+AL+DS play (std)")
group(lambda r: r["deck"] in core and r["depth"] == 5 and r["hg"], "CG+AL+DS 2HG")
group(lambda r: r["deck"] in core and r["depth"] == 3, "CG+AL+DS d3 b10")
group(lambda r: r["deck"] not in core and not r["hg"], "FLEET (10 decks) play (std)")
group(lambda r: r["deck"] not in core and r["hg"], "FLEET (10 decks) 2HG")
group(lambda r: r["depth"] == 5 and not r["hg"], "ALL 13 d5 decks play (std)")
group(lambda r: r["depth"] == 5 and r["hg"], "ALL 13 d5 decks 2HG")
