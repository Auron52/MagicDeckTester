#!/usr/bin/env python3
"""Pooled held-out A/B for MTG_MAZE_RANK_SPENDABLE on wip/maze-applydrop (DEFAULT ON, live only with
MTG_RANK_OBJECT_COLORS): (1) Maze's End's tap-order rank over the LIST's colours, (2) the new-name land-tutor slot
skips Talon Gates (its own slot) so the searched tutor axis is not sized off a collapsed one-name menu (g993's second
cause). FRESH seeds, never used before (mk_ab.py: 25200000/26200000/27200000 Maze's End, 25300000/27300000 Snow;
an earlier unrun version of this file: 25400000/26400000/27400000): 25500000/26500000/27500000. One 1-game job per (row, gi, arm);
ONE `mtg --batch`. Maze's End only: both halves live in MazesEndProvider, so no other deck (Snow included) can move
between `rank` and `new`; Snow's rank-vs-base question is the earlier A/B's.

  python3 logs/maze-applydrop/mk_ab_spend.py ab_spend.json [maze_play maze_2hg maze_d0]
  defaults: 2500 2000 4000

Arms (per-job heurarm flags):
  base  : MTG_RANK_OBJECT_COLORS off                                   (the committed tree's play)
  rank  : MTG_RANK_OBJECT_COLORS on,  MTG_MAZE_RANK_SPENDABLE off      (d4891c6e4's play: fix 1 alone)
  new   : MTG_RANK_OBJECT_COLORS on,  MTG_MAZE_RANK_SPENDABLE on       (the branch default: list-colour rank + Talon slot)
"""
import json, sys
MAZE = ("decks/Maze's End/Maze's End.cod", "decks/Maze's End/Maze's End.profile.json")
ARMS = {"base": {"MTG_RANK_OBJECT_COLORS": False, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": False},
        "rank": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": False},
        "new": {"MTG_RANK_OBJECT_COLORS": True, "MTG_MAZE_HOLD_UP": False, "MTG_MAZE_RANK_SPENDABLE": True}}
n = [int(x) for x in sys.argv[2:5]] if len(sys.argv) >= 5 else [2500, 2000, 4000]
ROWS = [("maze_play", 25500000, n[0], None, False),
        ("maze_2hg", 26500000, n[1], None, True),
        ("maze_d0", 27500000, n[2], 0, False)]
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
