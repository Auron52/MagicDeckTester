#!/usr/bin/env python3
"""Paired off/on replay of the rebased tier's slower Maze's End games: tier settings (repro) and d8 b0."""
import json, sys
DECK = "decks/Maze's End/Maze's End.cod"; PROF = "decks/Maze's End/Maze's End.profile.json"
G = [("sd5", 1001, [16, 41], "d5"),
     ("sd0", 1001, [444, 663, 806], "d0"),
     ("rd0", 2002, [33, 173, 249, 357, 555, 584, 594], "d0")]
units = int(sys.argv[2]) if len(sys.argv) > 2 else 20000000
jobs = []
for tag, base, gis, kind in G:
    for gi in gis:
        for arm, on in (("off", False), ("on", True)):
            b = {"deck": DECK, "profile": PROF, "games": 1, "seed": base + gi, "game_index": gi,
                 "flags": {"MTG_MAZE_SUMMIT_PAY_FIRST": on}}
            tier = dict(b, name=f"{tag}tier_g{gi}_{arm}")
            if kind == "d5": tier.update(budget_ms=20)
            else: tier.update(depth=0, budget_ms=0, ignore_play_profile=True)
            jobs.append(tier)
            jobs.append(dict(b, name=f"{tag}d8_g{gi}_{arm}", depth=8, budget_ms=0, ignore_play_profile=True,
                             abandon_units=units))
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs")
