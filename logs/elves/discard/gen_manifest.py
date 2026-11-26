#!/usr/bin/env python3
"""Elves 5i discard A/B: ONE pooled manifest, every arm paired by (seed, game index).

Arms (heurarm per-job flags, so one batch holds them all):
  off    -- shipped: inherited SelesnyaLifegain buckets (MTG_ELVES_BUCKET_DISCARD unset = OFF)
  on     -- the drafted Elves buckets (MTG_ELVES_BUCKET_DISCARD=1)
  worst  -- BOUND: the rollout cleanup sheds the LAST-ranked card of the shipped ranking (MTG_SHED_WORST)
  ncw    -- BOUND: the non-cleanup discard sites take the last-ranked card (MTG_NONCLEANUP_SHED_WORST)
Formats: elves (standard) and elves2hg (starting_life 30, opponent_heads 2), suite setting d5 b20 (the
regression harness's d5 row shape: no depth key, budget_ms 20).
"""
import json, sys
games = int(sys.argv[1]) if len(sys.argv) > 1 else 400
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 9600000
ARMS = {"off": {}, "on": {"MTG_ELVES_BUCKET_DISCARD": True},
        "worst": {"MTG_SHED_WORST": True}, "ncw": {"MTG_NONCLEANUP_SHED_WORST": True}}
FMTS = {"std": {}, "2hg": {"starting_life": 30, "opponent_heads": 2}}
jobs = []
for fmt, extra in FMTS.items():
    for arm, flags in ARMS.items():
        j = {"name": f"{arm}.{fmt}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
             "games": games, "seed": seed, "budget_ms": 20, "weight": 0}
        j.update(extra)
        if flags:
            j["flags"] = flags
        jobs.append(j)
json.dump({"jobs": jobs}, sys.stdout, indent=1)
sys.stdout.write("\n")
