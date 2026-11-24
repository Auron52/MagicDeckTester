#!/usr/bin/env python3
"""Pooled held-out A/B for the Maze's End spend-rank TIE-BREAKS on wip/maze-rank3 (final fix vs base).

The branch default = MTG_RANK_OBJECT_COLORS on + MTG_MAZE_RANK_SPENDABLE on, with the tie-breaks inside the spend
rank (a dead source ranks 5 + its colour count; +1 within a rung for the list's major colour). Base = rank off
(MTG_RANK_OBJECT_COLORS=0, the committed tree's play). The 50f807e9c arm (spend rank WITHOUT the tie-breaks) is NOT
selectable by flags any more: its only switch was a temporary diagnostic, removed before commit. Its comparison with
base is the earlier ab_new A/B (25500000 / 26500000 / 27500000).

FRESH seeds (grepped 2026-11-24: no manifest, script or doc uses them; earlier Maze A/Bs used 25100000-27500000
bases): 25700000 play / 26700000 2HG / 27700000 d0. One 1-game job per (row, gi, arm); ONE `mtg --batch`.
Maze's End only: the change lives in MazesEndProvider::ManaSourceRank (deck_spell_major_color is stamped on every
deck but read only there), so Snow and every other deck cannot move.

  python3 logs/maze-rank3/mk_ab_tb.py ab_tb.json [maze_play maze_2hg maze_d0]
  defaults: 2500 2000 1000  -> 11,000 jobs
Play rows run at the profile's play setting (d5 b20; no depth/budget override); d0 rows at --depth 0.
"""
import json, sys
MAZE = ("decks/Maze's End/Maze's End.cod", "decks/Maze's End/Maze's End.profile.json")
ARMS = {"base": {"MTG_RANK_OBJECT_COLORS": False, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": False},
        "new": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": True}}
n = [int(x) for x in sys.argv[2:5]] if len(sys.argv) >= 5 else [2500, 2000, 1000]
ROWS = [("maze_play", 25700000, n[0], None, False),
        ("maze_2hg", 26700000, n[1], None, True),
        ("maze_d0", 27700000, n[2], 0, False)]
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
