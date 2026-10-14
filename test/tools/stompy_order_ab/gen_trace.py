#!/usr/bin/env python3
"""One-game jobs (at the A/B cell's own settings) for every listed game, to trace both binaries.

usage: gen_trace.py <slower.txt> <out.json> [flags-json]
"""
import json
import os
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
DECK = os.path.join(WT, "decks/StompySurprise/StompySurprise.cod")
PROF = os.path.join(WT, "decks/StompySurprise/StompySurprise.profile.json")
CELLS = {"ship": {}, "d3": {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
         "d0": {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}}
flags = json.loads(sys.argv[3]) if len(sys.argv) > 3 else None
jobs = []
for row in open(sys.argv[1]):
    f = row.split()
    if not f:
        continue
    deck, cell, gi = f[0], f[1], int(f[2])
    j = {"name": "%s.%s.x.g%d" % (deck, cell, gi), "deck": DECK, "profile": PROF, "games": 1,
         "seed": S0 + gi, "game_index": gi}
    j.update(CELLS[cell])
    if deck.endswith("2hg"):
        j["starting_life"] = 30
        j["opponent_heads"] = 2
    if flags:
        j["flags"] = flags
    jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[2], "w"), indent=1)
print("%d jobs -> %s" % (len(jobs), sys.argv[2]))
