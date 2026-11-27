#!/usr/bin/env python3
"""mk_d8.py <ab.err> <m.json> <out.json> [armA=on] [armB=off]: d8 b0 recovery manifest for every SEARCHED game
that armA plays slower than armB. One single-game job per (game, arm), both arms, paired seed/game_index, so the
recovery read is: armA at d8 b0 reaches armB's shipped turn (or better). d0 cells are policy games, not d8-gated."""
import json, re, sys
err, man, out = sys.argv[1:4]
A = sys.argv[4] if len(sys.argv) > 4 else "on"
B = sys.argv[5] if len(sys.argv) > 5 else "off"
sc = lambda t: t if 0 < t <= 8 else 9
W = {}
for ln in open(err, errors="replace"):
    for m in re.finditer(r"\[win\] job=([^|]+)\|([^|]+)\|([^|]+)\|\d+ gi=(\d+) wt=(-?\d+)", ln):
        k, s, a, gi, wt = m.groups()
        W[(k, s, a, int(gi))] = int(wt)
proto = {}
for j in json.load(open(man))["jobs"]:
    k, s, a, gi0 = j["name"].split("|")
    proto.setdefault((k, s), j)
jobs = []
for (k, s, a, gi), wt in sorted(W.items()):
    if a != A or s == "d0" or (k, s, B, gi) not in W: continue
    base = W[(k, s, B, gi)]
    if sc(wt) <= sc(base): continue
    p = proto[(k, s)]
    for arm, on in ((A, A == "on"), (B, B == "on")):
        j = {kk: v for kk, v in p.items() if kk not in ("name", "games", "seed", "game_index", "weight", "flags")}
        j.update({"name": f"{k}|{s}|{arm}|{gi}|was{base}->{wt}", "games": 1, "seed": 7340000 + gi,
                  "game_index": gi, "depth": 8, "budget_ms": 0, "ignore_play_profile": True,
                  "flags": {"MTG_ATTACK_LORD_CAST": on}})
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=0)
print(len(jobs) // 2, "slower searched games ->", out)
