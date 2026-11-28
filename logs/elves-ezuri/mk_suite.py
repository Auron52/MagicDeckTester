#!/usr/bin/env python3
"""Digest check over the suite's Elves row shapes (smoke + regression seeds/depths/budgets): default vs MTG_ELVES_EZURI_COUNTS=0.
Not a GT reproduction -- a paired identity check (chunks of 10, arms innermost)."""
import json, sys
ROWS = [("elves",0,1001,1000,0),("elves",3,1001,10,10),("elves",5,1001,40,20),("elves2hg",5,1001,20,20),
        ("elves",0,2002,1000,0),("elves",3,2002,15,10),("elves",5,2002,60,20),("elves",5,3003,60,20),
        ("elves2hg",5,2002,30,20),("elves",3,30081,20,0),("elves",5,30081,20,0)]
jobs = []
for key, d, seed, n, b in ROWS:
    ch = 100 if d == 0 else 5
    for gi0 in range(0, n, ch):
        for arm, fl in (("def", {}), ("off", {"MTG_ELVES_EZURI_COUNTS": False})):
            j = {"name": f"{key}|d{d}s{seed}b{b}|{arm}|{gi0}", "deck": "decks/Elves/Elves.cod",
                 "profile": "decks/Elves/Elves.profile.json", "games": min(ch, n - gi0), "seed": seed + gi0,
                 "game_index": gi0, "depth": d, "budget_ms": b, "flags": fl}
            if d == 0: j["ignore_play_profile"] = True
            if key.endswith("2hg"): j.update({"starting_life": 30, "opponent_heads": 2})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
