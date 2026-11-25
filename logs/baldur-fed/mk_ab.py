#!/usr/bin/env python3
"""Pooled fresh-seed A/B for MTG_SCALED_ROUTE_ACTS (wip/baldur-fed) -- one `mtg --batch`, per-job flag arms.

  off = MTG_SCALED_ROUTE_ACTS false  (byte-identical to wip/maze-rank3 d624a0955: the change is wholly behind the flag)
  on  = MTG_SCALED_ROUTE_ACTS true   (the branch default)
Maze's End jobs carry the maze-rank3 branch defaults explicitly (RANK_OBJECT_COLORS + MAZE_RANK_SPENDABLE on,
MAZE_HOLD_UP off -- the arms the maze-rank3/4 A/Bs called "new"). Every job plays at its profile's play setting
(no depth/budget override). FRESH seeds: 39000000 Maze play, 39200000 Maze 2HG (30 life, two heads),
39400000 Merfolk Turns, 39600000 Goblins.

  python3 logs/baldur-fed/mk_ab.py <out.json> [n_maze n_2hg n_merfolk n_goblins]   defaults 5000 2000 2000 2000
Run from the worktree (relative deck paths): MTG_PGO=0 <bin> --batch <out.json> --threads $(nproc) --game-log-dir <dir>
"""
import json, sys
MAZE = ("decks/Maze's End/Maze's End.cod", "decks/Maze's End/Maze's End.profile.json")
MER = ("decks/Merfolk Turns/Merfolk Turns.cod", "decks/Merfolk Turns/Merfolk Turns.profile.json")
GOB = ("decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json")
MAZE_FLAGS = {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": True}
n = [int(x) for x in sys.argv[2:6]] if len(sys.argv) >= 6 else [5000, 2000, 2000, 2000]
ROWS = [("maze_play", MAZE, 39000000, n[0], False, MAZE_FLAGS),
        ("maze_2hg", MAZE, 39200000, n[1], True, MAZE_FLAGS),
        ("merfolkturns", MER, 39400000, n[2], False, {}),
        ("goblins", GOB, 39600000, n[3], False, {})]
jobs = []
for tag, dk, base, cnt, hg, extra in ROWS:
    for gi in range(cnt):
        for arm, v in (("off", False), ("on", True)):
            f = dict(extra); f["MTG_SCALED_ROUTE_ACTS"] = v
            j = {"name": f"{tag}_g{gi}_{arm}", "deck": dk[0], "profile": dk[1], "games": 1,
                 "seed": base + gi, "game_index": gi, "flags": f}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
