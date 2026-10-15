#!/usr/bin/env python3
"""Lever attribution for listed games: each game at its A/B cell under several new-binary arms.

usage: gen_attrib.py <slower.txt> <out.json> [--stage1]
Arms: new (defaults), notop (MTG_STOMPY_TOP_RESOLVE=0), nosearch (MTG_STOMPY_ORDER_SEARCH=0),
neither (both 0), off (MTG_STOMPY_ORDER=0). --stage1 runs every arm at --depth max(6, old) --budget-ms 100.
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
ARMS = {"new": None, "notop": {"MTG_STOMPY_TOP_RESOLVE": False}, "nosearch": {"MTG_STOMPY_ORDER_SEARCH": False},
        "neither": {"MTG_STOMPY_TOP_RESOLVE": False, "MTG_STOMPY_ORDER_SEARCH": False},
        "off": {"MTG_STOMPY_ORDER": False}}
stage1 = "--stage1" in sys.argv
jobs = []
for row in open(sys.argv[1]):
    f = row.split()
    if not f:
        continue
    deck, cell, gi, wo = f[0], f[1], int(f[2]), int(f[3])
    for an, fl in ARMS.items():
        j = {"name": "%s.%s.%s.g%d" % (deck, cell, an, gi), "deck": DECK, "profile": PROF, "games": 1,
             "seed": S0 + gi, "game_index": gi}
        if stage1:
            j.update({"depth": max(6, wo), "budget_ms": 100, "ignore_play_profile": True})
        else:
            j.update(CELLS[cell])
        if deck.endswith("2hg"):
            j["starting_life"] = 30
            j["opponent_heads"] = 2
        if fl:
            j["flags"] = fl
        jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[2], "w"), indent=1)
print("%d jobs -> %s" % (len(jobs), sys.argv[2]))
