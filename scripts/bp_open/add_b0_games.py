#!/usr/bin/env python3
"""Append to a (not yet started) manifest: the A/B's F5-slower games replayed at their OWN cell's depth with
--budget-ms 0, arms off / nodemote / on. on <= off there means the loss is finite-budget churn only.
  add_b0_games.py <manifest> <key>:<cell>:<gi> ...      (cell ship = value_play depth, d3 = depth 3)"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ab import ALL_DECKS, ARMS, PRIMARY, seed_base  # noqa: E402

DECK = {k: (d, p) for k, d, p in ALL_DECKS}
SEED0 = 10400001
man = sys.argv[1]
m = json.load(open(man))
names = {j["name"] for j in m["jobs"]}
add = []
for spec in sys.argv[2:]:
    key, cell, gi = spec.split(":")
    gi = int(gi)
    deck, prof = DECK[key]
    for arm in ("off", "nodemote", "on"):
        j = {"name": "%s_%s_%s_g%d_b0" % (key, cell, arm, gi), "deck": os.path.join(PRIMARY, deck),
             "profile": os.path.join(PRIMARY, prof), "games": 1, "seed": seed_base(SEED0, key, cell) + gi,
             "game_index": gi, "budget_ms": 0, "flags": dict(ARMS[arm])}
        if cell == "d3":
            j["depth"] = 3
            j["ignore_play_profile"] = True
        assert j["name"] not in names
        add.append(j)
m["jobs"] = add + m["jobs"]   # first: these are single long games
json.dump(m, open(man, "w"), indent=1)
print("added", len(add), "->", len(m["jobs"]), "jobs")
