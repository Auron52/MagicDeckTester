#!/usr/bin/env python3
"""Pooled held-out A/B for MTG_MAZE_RANK_SPENDABLE (Maze's End's tap-order rank over the LIST's colours; DEFAULT ON,
live only with MTG_RANK_OBJECT_COLORS) on wip/maze-applydrop. FRESH seeds (mk_ab.py used 25200000/26200000/27200000
for Maze's End; these use 25400000/26400000/27400000). One 1-game job per (row, gi, arm); ONE `mtg --batch`.
Maze's End only: the lever lives in MazesEndProvider::ManaSourceRank, so no other deck moves.

  python3 logs/maze-applydrop/mk_ab_spend.py ab_spend.json [maze_play maze_2hg maze_d0]
  defaults: 2500 2000 4000

Arms (per-job heurarm flags):
  base  : MTG_RANK_OBJECT_COLORS off                                   (the committed tree's play)
  rank  : MTG_RANK_OBJECT_COLORS on,  MTG_MAZE_RANK_SPENDABLE off      (fix 1 alone, as A/B'd before)
  spend : MTG_RANK_OBJECT_COLORS on,  MTG_MAZE_RANK_SPENDABLE on       (fix 1 + the list-colour rank; the branch default)
"""
import json, sys
MAZE = ("decks/Maze's End/Maze's End.cod", "decks/Maze's End/Maze's End.profile.json")
ARMS = {"base": {"MTG_RANK_OBJECT_COLORS": False, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": False},
        "rank": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": False},
        "spend": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": True}}
n = [int(x) for x in sys.argv[2:5]] if len(sys.argv) >= 5 else [2500, 2000, 4000]
ROWS = [("maze_play", 25400000, n[0], None, False),
        ("maze_2hg", 26400000, n[1], None, True),
        ("maze_d0", 27400000, n[2], 0, False)]
jobs = []
for tag, base, cnt, depth, hg in ROWS:
    for gi in range(cnt):
        for arm in ARMS:
            j = {"name": f"{tag}_g{gi}_{arm}", "deck": MAZE[0], "profile": MAZE[1], "games": 1,
                 "seed": base + gi, "game_index": gi, "flags": dict(ARMS[arm])}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            if depth is not None: j.update({"depth": depth, "budget_ms": 0, "ignore_play_profile": True})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
