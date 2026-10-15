#!/usr/bin/env python3
"""Play-identity check of the CastOrderTotalByName switch: a small manifest (StompySurprise under order +
fix, every cell, a few games; two other decks at d3) run on the old and the new fix binary, threads 1.
usage: ident_switch.py <out.json>"""
import json
import os
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
D = os.path.join(WT, "decks/StompySurprise/StompySurprise.cod")
P = os.path.join(WT, "decks/StompySurprise/StompySurprise.profile.json")
F = {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True}
jobs = [
    {"name": "s.ship", "deck": D, "profile": P, "games": 25, "seed": S0, "flags": F},
    {"name": "s.d3", "deck": D, "profile": P, "games": 60, "seed": S0, "depth": 3, "budget_ms": 10,
     "ignore_play_profile": True, "flags": F},
    {"name": "s.d0", "deck": D, "profile": P, "games": 400, "seed": S0, "depth": 0, "budget_ms": 0,
     "ignore_play_profile": True, "flags": F},
    {"name": "s2.d3", "deck": D, "profile": P, "games": 30, "seed": S0, "depth": 3, "budget_ms": 10,
     "ignore_play_profile": True, "flags": F, "starting_life": 30, "opponent_heads": 2},
    {"name": "dragonstorm.d3", "deck": os.path.join(WT, "decks/Dragonstorm/Dragonstorm.cod"),
     "profile": os.path.join(WT, "decks/Dragonstorm/Dragonstorm.profile.json"), "games": 30, "seed": S0,
     "depth": 3, "budget_ms": 10, "ignore_play_profile": True},
    {"name": "hinata.d0", "deck": os.path.join(WT, "decks/Hinata2/Hinata2.cod"),
     "profile": os.path.join(WT, "decks/Hinata2/Hinata2.profile.json"), "games": 200, "seed": S0,
     "depth": 0, "budget_ms": 0, "ignore_play_profile": True},
]
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
print(len(jobs))
