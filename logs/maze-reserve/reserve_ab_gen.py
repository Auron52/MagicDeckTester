#!/usr/bin/env python3
"""Maze's End hold-up A/B manifest, THREE arms on one binary (wip/maze-reserve), paired per game (same seed +
game_index in every arm), one pooled manifest:
  off      MTG_MAZE_HOLD_UP=0                            (the tree default)
  list     MTG_MAZE_HOLD_UP=1, MTG_MAZE_HOLD_RESERVE=0   (every U/G spell cost in hand, cheapest first, each alone)
  reserve  MTG_MAZE_HOLD_UP=1, MTG_MAZE_HOLD_RESERVE=1   (ONE joint reserve beyond the rest of the line: {G}{G}{U}
                                                          shaped by the hand, falling back {G}{G} -> {G} -> {U})
RANK_OBJECT_COLORS / MAZE_RANK_SPENDABLE are left at the tree's defaults; only the hold-up moves.
Fresh seed bases (Maze A/Bs so far used 25100000..28700000; grep of the repo, logs and build/scratch found none of these):
  play (d5 b20, the deck's play setting)      29400000 + gi, gi < play_n (default 2500)
  2HG  (starting_life 30, opponent_heads 2)   29600000 + gi, gi < hg_n   (default 2000)
  d0   (depth 0, budget 0, ignore profile)    29800000 + gi, gi < d0_n   (default 300)
Usage: reserve_ab_gen.py <out.json> [play_n] [hg_n] [d0_n]   (run from the repo root; do NOT run the batch here --
the coordinator runs it under the box lock). Then e.g.:
  build/Release/mtg --batch <out.json> --threads $(nproc) --game-log-dir build/scratch/<who>/maze_reserve_ab
Read per format and arm: games won / avg win turn, the paired per-game deltas (reserve - off, reserve - list,
list - off), and cpu= per arm (the hold-up's cost)."""
import json, sys
out = sys.argv[1]
play_n = int(sys.argv[2]) if len(sys.argv) > 2 else 2500
hg_n = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
d0_n = int(sys.argv[4]) if len(sys.argv) > 4 else 300
D = "decks/Maze's End/Maze's End.cod"
P = "decks/Maze's End/Maze's End.profile.json"
ARMS = {"off":     {"MTG_MAZE_HOLD_UP": False, "MTG_MAZE_HOLD_RESERVE": False},
        "list":    {"MTG_MAZE_HOLD_UP": True,  "MTG_MAZE_HOLD_RESERVE": False},
        "reserve": {"MTG_MAZE_HOLD_UP": True,  "MTG_MAZE_HOLD_RESERVE": True}}
FMTS = [("play", 29400000, play_n, {}),
        ("2hg", 29600000, hg_n, {"starting_life": 30, "opponent_heads": 2}),
        ("d0", 29800000, d0_n, {"depth": 0, "budget_ms": 0, "ignore_play_profile": True})]
jobs = []
for fmt, base, n, extra in FMTS:
    for gi in range(n):
        for arm, flags in ARMS.items():
            j = {"name": f"maze_{fmt}_g{gi}_{arm}", "deck": D, "profile": P, "games": 1,
                 "seed": base + gi, "game_index": gi, "flags": dict(flags)}
            j.update(extra)
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=0)
print(f"{out}: {len(jobs)} jobs ({play_n} play, {hg_n} 2HG, {d0_n} d0 games x {len(ARMS)} arms)")
