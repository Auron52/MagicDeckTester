#!/usr/bin/env python3
"""Pooled held-out 3-arm A/B (fresh seeds): off / pay-first+NAMED / pay-first+ALL3. One 1-game job per (row, gi, arm)."""
import json, sys
DECK = "decks/Maze's End/Maze's End.cod"
PROF = "decks/Maze's End/Maze's End.profile.json"
ARMS = {"off": {"MTG_MAZE_SUMMIT_PAY_FIRST": False},
        "named": {"MTG_MAZE_SUMMIT_PAY_FIRST": True, "MTG_MAZE_SUMMIT_PF_NAMED": True},
        "all3": {"MTG_MAZE_SUMMIT_PAY_FIRST": True, "MTG_MAZE_SUMMIT_PF_ALL3": True}}
ROWS = [("d5", 25100000, int(sys.argv[2]), None, 20, False),
        ("2hg", 26100000, int(sys.argv[3]), None, 20, True),
        ("d0", 27100000, int(sys.argv[4]), 0, 0, False)]
jobs = []
for tag, base, n, depth, bud, hg in ROWS:
    for gi in range(n):
        for arm, fl in ARMS.items():
            if tag == "d0" and arm == "all3": continue   # the menu is not searched at d0: ALL3 == pay-first there
            j = {"name": f"{tag}_g{gi}_{arm}", "deck": DECK, "profile": PROF, "games": 1,
                 "seed": base + gi, "game_index": gi, "budget_ms": bud, "flags": dict(fl)}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            if depth is not None: j.update({"depth": depth, "ignore_play_profile": True})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
