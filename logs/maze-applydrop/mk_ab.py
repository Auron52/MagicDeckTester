#!/usr/bin/env python3
"""Pooled held-out A/B for the Maze's End APPLY-DROP fixes (wip/maze-applydrop). FRESH seeds (the maze-summit A/Bs used
25100000/26100000/27100000/27000000; these use 25200000/26200000/27200000 for Maze's End and 25300000/27300000 for Snow).
One 1-game job per (row, gi, arm); ONE `mtg --batch`.

  python3 logs/maze-applydrop/mk_ab.py ab.json [maze_play maze_2hg maze_d0 snow_play snow_d0]
  defaults: 2500 2000 4000 2000 4000

Arms (per-job heurarm flags):
  base : MTG_RANK_OBJECT_COLORS off, MTG_MAZE_HOLD_UP off   (the committed tree's play)
  rank : MTG_RANK_OBJECT_COLORS on                          (fix 1, DEFAULT ON on the branch)
  hold : MTG_RANK_OBJECT_COLORS on + MTG_MAZE_HOLD_UP on    (fix 2, DEFAULT OFF; Maze's End only -- the hold-up is the
         Maze's End provider's, so on Snow it is the rank arm and is not run)
Rows: Maze's End at its play setting (default resolution: d5 b20), 2HG (30 life, 2 heads, same setting), d0; Snow
(Coldsteel Heart is its one colour-locked source, the only other deck the rank fix can move) at its play setting and d0.
"""
import json, sys
MAZE = ("decks/Maze's End/Maze's End.cod", "decks/Maze's End/Maze's End.profile.json")
SNOW = ("decks/Snow/Snow.cod", "decks/Snow/Snow.profile.json")
ARMS = {"base": {"MTG_RANK_OBJECT_COLORS": False, "MTG_MAZE_HOLD_UP": False},
        "rank": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False},
        "hold": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": True}}
n = [int(x) for x in sys.argv[2:7]] if len(sys.argv) >= 7 else [2500, 2000, 4000, 2000, 4000]
ROWS = [("maze_play", MAZE, 25200000, n[0], None, False, ("base", "rank", "hold")),
        ("maze_2hg", MAZE, 26200000, n[1], None, True, ("base", "rank", "hold")),
        ("maze_d0", MAZE, 27200000, n[2], 0, False, ("base", "rank", "hold")),
        ("snow_play", SNOW, 25300000, n[3], None, False, ("base", "rank")),
        ("snow_d0", SNOW, 27300000, n[4], 0, False, ("base", "rank"))]
jobs = []
for tag, (deck, prof), base, cnt, depth, hg, arms in ROWS:
    for gi in range(cnt):
        for arm in arms:
            j = {"name": f"{tag}_g{gi}_{arm}", "deck": deck, "profile": prof, "games": 1,
                 "seed": base + gi, "game_index": gi, "flags": dict(ARMS[arm])}
            if hg: j.update({"starting_life": 30, "opponent_heads": 2})
            if depth is not None: j.update({"depth": depth, "budget_ms": 0, "ignore_play_profile": True})
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
