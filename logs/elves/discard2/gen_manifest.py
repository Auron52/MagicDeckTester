#!/usr/bin/env python3
"""Elves cleanup-discard A/B (the USER's mana/threats buckets, 2026-11-27): ONE pooled manifest.
Arms on (MTG_ELVES_BUCKET_DISCARD=1, the new default) vs off (=0, the inherited SelesnyaLifegain buckets),
ARMS INNERMOST per chunk, every chunk paired by seed = BASE + off AND game_index = off.
usage: gen_manifest.py <games_per_format> <base_seed> [chunk] > ab.json"""
import json, sys
games = int(sys.argv[1]); base = int(sys.argv[2]); ch = int(sys.argv[3]) if len(sys.argv) > 3 else 50
FMTS = {"std": {}, "2hg": {"starting_life": 30, "opponent_heads": 2}}
ARMS = {"on": True, "off": False}
jobs = []
for off in range(0, games, ch):
    for fmt, extra in FMTS.items():
        for arm, val in ARMS.items():
            j = {"name": f"{arm}.{fmt}.c{off}", "deck": "decks/Elves/Elves.cod",
                 "profile": "decks/Elves/Elves.profile.json", "games": min(ch, games - off),
                 "seed": base + off, "game_index": off, "budget_ms": 20, "weight": 0,
                 "flags": {"MTG_ELVES_BUCKET_DISCARD": val}}
            j.update(extra)
            jobs.append(j)
json.dump({"jobs": jobs}, sys.stdout, indent=1)
sys.stdout.write("\n")
