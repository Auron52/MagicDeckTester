#!/usr/bin/env python3
"""Pooled fresh-seed A/B for the Maze's End spend rank on wip/maze-rank4 (branch default vs base).

Arms (per-job flags, the mk_ab_tb.py pattern):
  base = MTG_RANK_OBJECT_COLORS off, MTG_MAZE_RANK_SPENDABLE off (the committed tree's play)
  new  = MTG_RANK_OBJECT_COLORS on,  MTG_MAZE_RANK_SPENDABLE on  (the branch default; see logs/maze-rank4/report.md)
MTG_MAZE_HOLD_UP is pinned off on both arms (default-OFF lever awaiting its own A/B).

FRESH seeds (coordinator, 2026-11-25): 38000000 play (profile play setting, d5 b20; no depth/budget override) and
38200000 2HG (30 life, two heads, play setting). One 1-game job per (row, gi, arm); ONE `mtg --batch`.
Maze's End only: the change lives in MazesEndProvider, so no other deck can move.

  python3 logs/maze-rank4/mk_ab_r4.py <out.json> [n_play n_2hg]     defaults: 5000 2000 -> 14,000 jobs
Run (from the worktree, so the relative deck paths resolve):
  MTG_PGO=0 <bin> --batch <out.json> --threads $(nproc) --game-log-dir <dir>
"""
import json, sys
MAZE = ("decks/Maze's End/Maze's End.cod", "decks/Maze's End/Maze's End.profile.json")
ARMS = {"base": {"MTG_RANK_OBJECT_COLORS": False, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": False},
        "new":  {"MTG_RANK_OBJECT_COLORS": True,  "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": True}}
n = [int(x) for x in sys.argv[2:4]] if len(sys.argv) >= 4 else [5000, 2000]
ROWS = [("maze_play", 38000000, n[0], False), ("maze_2hg", 38200000, n[1], True)]
jobs = []
for tag, base, cnt, hg in ROWS:
    for gi in range(cnt):
        for arm in ARMS:
            j = {"name": f"{tag}_g{gi}_{arm}", "deck": MAZE[0], "profile": MAZE[1], "games": 1,
                 "seed": base + gi, "game_index": gi, "flags": dict(ARMS[arm])}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
