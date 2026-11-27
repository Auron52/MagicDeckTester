#!/usr/bin/env python3
"""r3_mk_d8.py <ab.out> <out.json> [arm=bandtapfix] [ref=band]: d8 b0 jobs for every game SLOWER in `arm` than `ref`.
Both arms run at --depth 8 --budget-ms 0 with their own flags, so "recovers" = the slower arm at d8 b0 wins no later
than the reference did at d5 b20 (and the reference's own d8 b0 turn is printed beside it)."""
import json, re, sys, collections, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r3_ab_manifest import ARMS
FL = dict(ARMS)
arm = sys.argv[3] if len(sys.argv) > 3 else "bandtapfix"
ref = sys.argv[4] if len(sys.argv) > 4 else "band"
R = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    m = re.match(r'(\w+?)_(\w+)@(\d+): played=1 avg=([\d.]+)', line)
    if m: R[(m[1], int(m[3]))][m[2]] = float(m[4])
jobs = []
for (fmt, gi), a in sorted(R.items()):
    if arm in a and ref in a and a[arm] > a[ref]:
        for tag in (arm, ref):
            j = {"name": f"{fmt}_{tag}@{gi}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
                 "ignore_play_profile": True, "games": 1, "seed": 9610001 + gi, "game_index": gi, "depth": 8,
                 "budget_ms": 0, "flags": dict(FL[tag])}
            if fmt == "2hg":
                j["starting_life"] = 30; j["opponent_heads"] = 2
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[2], "w"), indent=1)
print(len(jobs), "d8 b0 jobs ->", sys.argv[2])
