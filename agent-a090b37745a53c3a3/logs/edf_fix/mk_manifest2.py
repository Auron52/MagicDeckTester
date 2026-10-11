#!/usr/bin/env python3
"""Held-out manifest, run 2 (resized for the contended box). Same seeds and binary as run 1, so the
ship_on games 0..39 that run 1 finished are REUSED (deterministic per game) and not re-run here.
  ship (d5/b20, profile): 40 games x {L1, L2a, L2b, L3, L4}            seed 5320001
  d3 (b10):               80 games x {on, L1, L2a, L2b, L3, L4}        seed 5310001
  d0:                   1500 games x {on, L1, L2a, L2b, L3, L4, off}   seed 5300001
"""
import json, sys
sys.path.insert(0, "/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3/logs/edf_fix")
from mk_manifest import DECK, PROF, arm_flags

out = sys.argv[1]
cells = [("ship", 40, 5320001, {"budget_ms": 20}, ["L1", "L2a", "L2b", "L3", "L4"]),
         ("d3", 80, 5310001, {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
          ["on", "L1", "L2a", "L2b", "L3", "L4"]),
         ("d0", 1500, 5300001, {"depth": 0, "ignore_play_profile": True},
          ["on", "L1", "L2a", "L2b", "L3", "L4", "off"])]
jobs = []
for cell, n, seed, extra, arms in cells:
    for arm in arms:
        j = {"name": "edf_%s_%s" % (cell, arm), "deck": DECK, "profile": PROF, "games": n, "seed": seed}
        j.update(extra)
        f = arm_flags(arm)
        if f:
            j["flags"] = f
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print("wrote %s: %d jobs" % (out, len(jobs)))
