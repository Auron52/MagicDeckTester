#!/usr/bin/env python3
"""r2_mk_d8.py <ab.out> <out.json>: d8 b0 jobs for every game SLOWER in an arm than its reference (band vs origin; three,
two, twotap, bandtap vs band AND vs origin). Each (game, arm) is run once at --depth 8 --budget-ms 0 with that arm's
flags; the reference arm is run too, so "recovers" = the slower arm at d8 b0 wins no later than the reference did."""
import json, re, sys, collections, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
R = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    m = re.match(r'(\w+?)_(\w+)@(\d+): played=1 avg=([\d.]+)', line)
    if m: R[(m[1], int(m[3]))][m[2]] = float(m[4])
R2 = {"MTG_ELVES_R2": True}
FL = {"origin": {"MTG_ELVES_R2": False}, "band": dict(R2), "three": dict(R2, MTG_ELVES_HOLDBACK_THREE=True),
      "two": dict(R2, MTG_ELVES_HOLDBACK_TWO=True),
      "twotap": dict(R2, MTG_ELVES_HOLDBACK_TWO=True, MTG_ELVES_TAP_ORDER=True),
      "bandtap": dict(R2, MTG_ELVES_TAP_ORDER=True)}
pairs = [("band", "origin")] + [(a, r) for a in ("three", "two", "twotap", "bandtap") for r in ("band", "origin")]
need = set()
for (fmt, gi), arms in R.items():
    for a, r in pairs:
        if a in arms and r in arms and arms[a] > arms[r]:
            need.add((fmt, gi, a)); need.add((fmt, gi, r))
jobs = []
for fmt, gi, a in sorted(need):
    j = {"name": f"{fmt}_{a}@{gi}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
         "ignore_play_profile": True, "games": 1, "seed": 9610001 + gi, "game_index": gi, "depth": 8, "budget_ms": 0,
         "flags": dict(FL[a])}
    if fmt == "2hg":
        j["starting_life"] = 30; j["opponent_heads"] = 2
    jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[2], "w"), indent=1)
print(len(jobs), "d8 b0 jobs ->", sys.argv[2])
