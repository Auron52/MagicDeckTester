#!/usr/bin/env python3
"""Pooled held-out A/B manifest for MTG_MAZE_SUMMIT_PAY_FIRST: one 1-game job per (row, gi, arm), arms innermost."""
import json, sys
DECK = "decks/Maze's End/Maze's End.cod"
PROF = "decks/Maze's End/Maze's End.profile.json"
# (tag, base seed, games, depth or None (= play settings d5 via the profile), budget, 2hg)
ROWS = [("d5", 25000000, int(sys.argv[2]) if len(sys.argv) > 2 else 2000, None, 20, False),
        ("2hg", 26000000, int(sys.argv[3]) if len(sys.argv) > 3 else 500, None, 20, True),
        ("d0", 27000000, int(sys.argv[4]) if len(sys.argv) > 4 else 4000, 0, 0, False)]
jobs = []
for tag, base, n, depth, bud, hg in ROWS:
    for gi in range(n):
        for arm in (0, 1):
            j = {"name": f"{tag}_g{gi}_{'on' if arm else 'off'}", "deck": DECK, "profile": PROF, "games": 1,
                 "seed": base + gi, "game_index": gi, "budget_ms": bud,
                 "flags": {"MTG_MAZE_SUMMIT_PAY_FIRST": bool(arm)}}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            if depth is not None: j.update({"depth": depth, "ignore_play_profile": True})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
