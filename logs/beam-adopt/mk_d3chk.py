#!/usr/bin/env python3
"""Task 2 pre-check (2026-11-25): is d3 b10 play byte-identical with and without the depth-3 static W20 beam, K OFF?
Arms on today's binary (wip/beam-staged b584c7a16): A (today) vs N (MTG_ESC_BEAM_NONE; at depth 3 that removes exactly
the W20 regime). Rows: d3 b10 ignore_play_profile, the suite's d3 shape. Seeds 48,100,000+."""
import json, os, sys
R = "/workspaces/MagicDeckTester/.claude/worktrees/beam-staged"
OUT = "/workspaces/MagicDeckTester/build/scratch/beam-adopt/d3chk"
DECKS = {"creature_giving": "decks/Creature Giving/Creature Giving", "knights": "decks/Knights/Knights",
         "vampires": "decks/Vampires/Vampires", "giants": "decks/Giants/Giants"}
ARMS = {"A": {}, "N": {"MTG_ESC_BEAM_NONE": True}}
jobs, k = [], 0
for key, stem in DECKS.items():
    ext = ".cod" if os.path.exists(os.path.join(R, stem + ".cod")) else ".txt"
    for c in range(0, 300, 100):
        seed = 48100000 + 1000 * k; k += 1
        for arm, fl in ARMS.items():
            j = {"name": f"{key}_d3b10_{arm}_s{seed}", "deck": os.path.join(R, stem + ext),
                 "profile": os.path.join(R, stem + ".profile.json"), "games": 100, "seed": seed, "game_index": 0,
                 "depth": 3, "budget_ms": 10, "ignore_play_profile": True}
            if fl: j["flags"] = dict(fl)
            jobs.append(j)
json.dump({"jobs": jobs}, open(os.path.join(OUT, "m.json"), "w"), indent=1)
print(len(jobs), "jobs")
