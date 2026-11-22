#!/usr/bin/env python3
"""d8 b0 paired replay (off vs pay-first+ALL3) of every slower game of the adopted arm. Seeds as in mk_ab2 / mk_ab."""
import json, sys
DECK = "decks/Maze's End/Maze's End.cod"; PROF = "decks/Maze's End/Maze's End.profile.json"
G = [("d5", 25100000, [331, 513, 607, 973, 1094, 1585, 1667, 2021, 2349], False),
     ("2hg", 26100000, [137, 292], True),
     ("d0", 27000000, [170, 266, 336, 360, 532, 561, 767, 872, 891, 920, 1081, 1589, 2222, 2505, 2919, 2981, 3105,
                       3113, 3529, 3646, 3730, 3959, 3985], False)]
ARMS = {"off": {"MTG_MAZE_SUMMIT_PAY_FIRST": False},
        "all3": {"MTG_MAZE_SUMMIT_PAY_FIRST": True, "MTG_MAZE_SUMMIT_PF_ALL3": True}}
units = int(sys.argv[2]) if len(sys.argv) > 2 else 20000000
jobs = []
for tag, base, gis, hg in G:
    for gi in gis:
        for arm, fl in ARMS.items():
            j = {"name": f"{tag}_g{gi}_{arm}", "deck": DECK, "profile": PROF, "games": 1, "seed": base + gi,
                 "game_index": gi, "budget_ms": 0, "depth": 8, "ignore_play_profile": True,
                 "abandon_units": units, "flags": dict(fl)}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
