#!/usr/bin/env python3
"""Report for the beam depth-gate A/B made by mk_ab.py. Pairs every arm against A per game (seed + game index; a
loss counts as 9), substituting an arm that is identical by construction (rows.json "equiv") with the arm that
represents it, and sums thread CPU / units per arm from batch.out's job lines.
usage: ab_report.py <outdir>   (outdir holds rows.json, wins/, batch.out)   [--list]  lists every game that moves
"""
import json, math, os, re, sys

out = sys.argv[1]
rows = json.load(open(os.path.join(out, "rows.json")))
jobline = re.compile(r"^(\S+): played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+) cpu=(\d+)")
jobs = {}
for ln in open(os.path.join(out, "batch.out")):
    m = jobline.match(ln)
    if m:
        jobs[m.group(1)] = dict(played=int(m.group(2)), units=int(m.group(4)), cpu=int(m.group(5)))


def load(row, arm):
    res, played, cpu, units = {}, 0, 0, 0
    pre = f"{row}_{arm}_s"
    for f in os.listdir(os.path.join(out, "wins")):
        if not (f.startswith(pre) and f.endswith(".wins")):
            continue
        name = f[:-5]
        seed = int(name[len(pre):])
        for ln in open(os.path.join(out, "wins", f)):
            p = ln.split()
            if len(p) >= 2:
                t = int(p[1])
                res[(seed, int(p[0]))] = (9 if t < 0 else t, p[2] if len(p) > 2 else "")
        if name in jobs:
            played += jobs[name]["played"]; cpu += jobs[name]["cpu"]; units += jobs[name]["units"]
    return res, played, cpu, units


tot = {}
for r in rows["rows"]:
    row, eq = r["row"], rows["equiv"][r["row"]]
    data = {arm: load(row, arm) for arm in set(eq.values())}
    base, bp, bc, bu = data[eq["A"]]
    print(f"== {row} (d{r['depth']}{'' if r['budget'] is None else ' b%d' % r['budget']}, run {r['run']}, "
          f"regimes {r['regimes']})")
    for arm in sorted(eq):
        res, pl, cpu, units = data[eq[arm]]
        keys = sorted(set(base) & set(res))
        if not keys:
            print(f"   {arm}: no results"); continue
        ds = [res[k][0] - base[k][0] for k in keys]
        n = len(ds); m = sum(ds) / n
        sd = math.sqrt(sum((x - m) ** 2 for x in ds) / max(1, n - 1)); se = sd / math.sqrt(n)
        avg = sum(res[k][0] for k in keys) / n
        same = sum(res[k][1] == base[k][1] for k in keys)
        ident = "" if eq[arm] == arm else f" (== {eq[arm]} by construction)"
        cpug = cpu / pl if pl else float("nan"); ug = units / pl if pl else float("nan")
        print(f"   {arm}: n={n} avg={avg:.4f} d_vs_A={m:+.4f} se={se:.4f} t={(m / se if se else 0):+.2f} "
              f"faster={sum(x < 0 for x in ds)} slower={sum(x > 0 for x in ds)} digests_eq_A={same}/{n} "
              f"cpu_ms/game={cpug:.0f} units/game={ug:.0f}{ident}")
        t = tot.setdefault(arm, [0, 0]); t[0] += cpu; t[1] += pl
        if "--list" in sys.argv and arm != "A":
            for kk in keys:
                if res[kk][0] != base[kk][0]:
                    print(f"      seed {kk[0]} gi {kk[1]}: A {base[kk][0]} -> {arm} {res[kk][0]}")
print("CPU per arm over every row (identical-by-construction rows counted at the representative's cost):")
for arm, (c, p) in sorted(tot.items()):
    print(f"   {arm}: {c / 1000:.0f} core-s over {p} games")
