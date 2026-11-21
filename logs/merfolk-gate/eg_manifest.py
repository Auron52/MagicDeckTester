#!/usr/bin/env python3
"""ENUM-GAP held-out A/B manifest: base (all off) vs option a (bound + decider) vs option b (bound + Merfolk rule).

usage: eg_manifest.py <out.json> [deck=games ...]   (default: merfolk=2000 goblins=2000)
Paired: seed = BASE + off and game_index = off on every arm; play settings from each deck's profile.
"""
import json
import sys

BASE = 34000000
DECKS = {"merfolk": "decks/Merfolk Turns/Merfolk Turns", "goblins": "decks/Goblins/Goblins"}
counts = {"merfolk": 2000, "goblins": 2000}
for a in sys.argv[2:]:
    k, v = a.split("=")
    counts[k] = int(v)
CHUNK = 25
ARMS = {
    "off": {"MTG_SCALED_GROWTH_BOUND": False, "MTG_SCALED_GROWER_DECIDER": False, "MTG_MERFOLK_GROWER_ORDER": False},
    "a": {"MTG_SCALED_GROWTH_BOUND": True, "MTG_SCALED_GROWER_DECIDER": True, "MTG_MERFOLK_GROWER_ORDER": False},
    "b": {"MTG_SCALED_GROWTH_BOUND": True, "MTG_SCALED_GROWER_DECIDER": False, "MTG_MERFOLK_GROWER_ORDER": True},
}
jobs = []
for key, stem in DECKS.items():
    n = counts.get(key, 0)
    for off in range(0, n, CHUNK):
        g = min(CHUNK, n - off)
        items = list(ARMS.items())
        r = (off // CHUNK) % 3
        for arm, flags in items[r:] + items[:r]:
            jobs.append({"name": f"{arm}__{key}__o{off}", "deck": stem + ".cod", "profile": stem + ".profile.json",
                         "games": g, "seed": BASE + off, "game_index": off, "flags": flags})
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
print(len(jobs), "jobs")
