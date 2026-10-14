#!/usr/bin/env python3
"""Recovery manifest for the slower games of the StompySurprise order A/B.

usage: gen_recovery.py <slower.txt> <stage: 1|2> <out.json> [--only FILE]
  stage 1: --depth W --budget-ms 100, W = max(6 (the shipped depth), the old win turn)
  stage 2: --depth 8 --budget-ms 0 (unlimited)
slower.txt lines: <deck> <cell> <gi> <old_wt> <new_wt>. --only restricts to the games listed in FILE.
"""
import json
import os
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
DECK = os.path.join(WT, "decks/StompySurprise/StompySurprise.cod")
PROF = os.path.join(WT, "decks/StompySurprise/StompySurprise.profile.json")

rows = [l.split() for l in open(sys.argv[1]) if l.strip()]
stage = int(sys.argv[2])
only = None
if "--only" in sys.argv:
    only = set(tuple(l.split()[:3]) for l in open(sys.argv[sys.argv.index("--only") + 1]) if l.strip())
jobs = []
for deck, cell, gi, wo, wn in rows:
    if only is not None and (deck, cell, gi) not in only:
        continue
    gi, wo = int(gi), int(wo)
    j = {"name": "%s.%s.s%d.g%d" % (deck, cell, stage, gi), "deck": DECK, "profile": PROF,
         "games": 1, "seed": S0 + gi, "game_index": gi, "ignore_play_profile": True}
    if stage == 1:
        j["depth"] = max(6, wo)
        j["budget_ms"] = 100
    else:
        j["depth"] = 8
        j["budget_ms"] = 0
    if deck.endswith("2hg"):
        j["starting_life"] = 30
        j["opponent_heads"] = 2
    jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[3], "w"), indent=1)
print("%d jobs -> %s" % (len(jobs), sys.argv[3]))
