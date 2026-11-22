#!/usr/bin/env python3
"""ENUM-GAP final held-out A/B on the shipped candidate: off vs the winner arm, searched (play profile) AND d0.

usage: eg_manifest3.py <out.json> <arm=a|b> [deck=games ...]   (default merfolk=2000 goblins=2000)
Fresh seeds (BASE 35000000, disjoint from the 34000000 selection A/B). Paired: seed = BASE + off, game_index = off.
Job names: <arm>_<mode>__<deck>__o<off>, mode s (searched) or d0, so eg_compare.py reads arm "a_s" vs "off_s".
"""
import json
import sys

BASE = 35000000
DECKS = {"merfolk": "decks/Merfolk Turns/Merfolk Turns", "goblins": "decks/Goblins/Goblins"}
win = sys.argv[2]
counts = {"merfolk": 2000, "goblins": 2000}
for a in sys.argv[3:]:
    k, v = a.split("=")
    counts[k] = int(v)
F = ("MTG_SCALED_GROWTH_BOUND", "MTG_SCALED_GROWER_DECIDER", "MTG_MERFOLK_GROWER_ORDER")
ARMS = {"off": (0, 0, 0), win: (1, 1, 0) if win == "a" else (1, 0, 1)}
CHUNK = 25
jobs = []
for key, stem in DECKS.items():
    for off in range(0, counts.get(key, 0), CHUNK):
        g = min(CHUNK, counts[key] - off)
        for mode in ("s", "d0"):
            for arm, v in ARMS.items():
                j = {"name": f"{arm}_{mode}__{key}__o{off}", "deck": stem + ".cod", "profile": stem + ".profile.json",
                     "games": g, "seed": BASE + off, "game_index": off, "flags": {f: bool(x) for f, x in zip(F, v)}}
                if mode == "d0":
                    j["depth"] = 0
                    j["budget_ms"] = 0
                    j["ignore_play_profile"] = True
                jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
print(len(jobs), "jobs")
