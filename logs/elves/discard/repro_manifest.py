#!/usr/bin/env python3
"""d8 b0 repro manifest for every SLOWER game of each arm vs the base arm in the Elves discard A/B.

usage: repro_manifest.py <ab.err> <base> <arm> [arm...] > repro.json
Each slower (fmt, gi) gets one single-game job per arm involved (base + every arm in which it was slower),
at depth 8, budget_ms 0, seed 9600000+gi, game_index gi -- the `--seed base+gi --game-index gi` form.
Job names: <arm>.<fmt>.g<gi>, so the batch's [win] lines read back as job=<arm>.<fmt>.g<gi>.
"""
import json, re, sys
from collections import defaultdict
SEED = 9600000
ARMS = {"off": {}, "on": {"MTG_ELVES_BUCKET_DISCARD": True},
        "worst": {"MTG_SHED_WORST": True}, "ncw": {"MTG_NONCLEANUP_SHED_WORST": True}}
FMTS = {"std": {}, "2hg": {"starting_life": 30, "opponent_heads": 2}}
pat = re.compile(r"\[win\] job=(\S+?)\.(\S+) gi=(\d+) wt=(-?\d+)")
res = defaultdict(dict)
for line in open(sys.argv[1], errors="ignore"):
    for m in pat.finditer(line):
        res[m.group(1)][(m.group(2), int(m.group(3)))] = int(m.group(4))
base, arms = sys.argv[2], sys.argv[3:]
sc = lambda w: 9 if w < 0 else w
need = defaultdict(set)   # (fmt, gi) -> arms to replay
for arm in arms:
    for k, w in res[arm].items():
        if k in res[base] and sc(w) > sc(res[base][k]):
            need[k].update({base, arm})
jobs = []
for (fmt, gi), want in sorted(need.items()):
    for arm in sorted(want):
        j = {"name": f"{arm}.{fmt}.g{gi}", "deck": "decks/Elves/Elves.cod",
             "profile": "decks/Elves/Elves.profile.json", "games": 1, "seed": SEED + gi, "game_index": gi,
             "depth": 8, "budget_ms": 0}
        j.update(FMTS[fmt])
        if ARMS[arm]:
            j["flags"] = ARMS[arm]
        jobs.append(j)
sys.stderr.write(f"{len(need)} slower games, {len(jobs)} jobs\n")
json.dump({"jobs": jobs}, sys.stdout, indent=1)
sys.stdout.write("\n")
