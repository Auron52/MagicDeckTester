#!/usr/bin/env python3
"""Maze's End hold-up final A/B (wip/maze-holdup): ONE binary, two arms paired per game (same seed + game_index):
  off  MTG_MAZE_HOLD_UP=0   (the hold-up off: origin's payer for this deck)
  on   the tree default (MTG_MAZE_HOLD_UP ON)
Fresh seed bases (Maze manifests so far used 250..300, 342, 380, 382 x 1e5):
  play (d5 b20, the deck's play setting)    31100000 + gi, gi < play_n (default 5000)
  2HG  (starting_life 30, opponent_heads 2) 31300000 + gi, gi < hg_n   (default 2000)
Usage: ab_gen.py <out.json> [play_n] [hg_n]   (run from the worktree root)."""
import json, sys
out = sys.argv[1]
play_n = int(sys.argv[2]) if len(sys.argv) > 2 else 5000
hg_n = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
D = "decks/Maze's End/Maze's End.cod"
P = "decks/Maze's End/Maze's End.profile.json"
ARMS = {"off": {"MTG_MAZE_HOLD_UP": False}, "on": {}}
FMTS = [("play", 31100000, play_n, {}), ("2hg", 31300000, hg_n, {"starting_life": 30, "opponent_heads": 2})]
jobs = []
for fmt, base, n, extra in FMTS:
    for gi in range(n):
        for arm, flags in ARMS.items():
            j = {"name": f"maze_{fmt}_g{gi}_{arm}", "deck": D, "profile": P, "games": 1,
                 "seed": base + gi, "game_index": gi}
            if flags: j["flags"] = dict(flags)
            j.update(extra)
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=0)
print(f"{out}: {len(jobs)} jobs ({play_n} play, {hg_n} 2HG games x {len(ARMS)} arms)")
