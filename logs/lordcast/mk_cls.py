#!/usr/bin/env python3
"""mk_cls.py <ab2.err> <m2.json> <out.json> <key> <shape> <armA> <armB> <arms-spec>: a per-game manifest that replays
every game where armA is SLOWER than armB in cell (key, shape) under each extra arm.
arms-spec: "name:FLAG=0|1,FLAG=0|1;name2:..." (flags as env names). Seeds: chunk BASE + gi, game_index gi, games 1."""
import collections, json, re, sys
err, man, out, key, shape, A, B, spec = sys.argv[1:9]
BASE = 7460000
W = collections.defaultdict(dict)
pat = re.compile(r"\[win\] job=([^|]+)\|([^|]+)\|([^|]+)\|\d+ gi=(\d+) wt=(-?\d+)")
for ln in open(err, errors="replace"):
    for m in pat.finditer(ln):
        k, s, a, gi, wt = m.groups()
        if k == key and s == shape and a in (A, B): W[a][int(gi)] = int(wt)
sc = lambda t: t if 0 < t <= 8 else 9
slow = sorted(g for g in W[A] if g in W[B] and sc(W[A][g]) > sc(W[B][g]))
tmpl = next(j for j in json.load(open(man))["jobs"] if j["name"].startswith(f"{key}|{shape}|"))
arms = []
for a in spec.split(";"):
    n, fl = a.split(":", 1)
    arms.append((n, {f.split("=")[0]: f.split("=")[1] == "1" for f in fl.split(",") if f}))
jobs = []
for g in slow:
    for n, fl in arms:
        j = dict(tmpl); j.update(name=f"{key}|{shape}|{n}|{g}", games=1, seed=BASE + g, game_index=g, flags=fl, weight=0)
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=0)
print(f"{len(slow)} slower games, {len(jobs)} jobs -> {out}", file=sys.stderr)
