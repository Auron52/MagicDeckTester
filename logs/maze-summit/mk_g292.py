#!/usr/bin/env python3
"""g292 (2HG, seed 26100292, gi 292, 30 life, 2 heads) escalated-budget classification, paired off vs on.
argv: out.json budgets(comma) [abandon_units]. Depth from the play profile (the A/B's 2HG row omits depth)."""
import json, sys
DECK = "decks/Maze's End/Maze's End.cod"; PROF = "decks/Maze's End/Maze's End.profile.json"
budgets = [int(b) for b in sys.argv[2].split(",")]
units = int(sys.argv[3]) if len(sys.argv) > 3 else 200000000
jobs = []
for b in budgets:
    for arm, on in (("off", False), ("on", True)):
        jobs.append({"name": f"2hg_b{b}_g292_{arm}", "deck": DECK, "profile": PROF, "games": 1, "seed": 26100292,
                     "game_index": 292, "budget_ms": b, "abandon_units": units,
                     "flags": {"MTG_MAZE_SUMMIT_PAY_FIRST": on}, "starting_life": 30, "opponent_heads": 2})
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
