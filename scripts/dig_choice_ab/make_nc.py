#!/usr/bin/env python3
"""Clairvoyance-stripped A/B: the SAME lever arms ('new' vs 'dig0'), played by the NON-CLAIRVOYANT search
(MTG_NC_SEARCH=1: every real decision ranks its candidate plans by the win turn AVERAGED over K reshuffles
of the unseen library -- the dig's draw is a random card, not the known top card -- then executes the
best against the true library). Env-level, so it is its own process; run with
  MTG_NC_SEARCH=1 MTG_NC_K=<K> MTG_NC_DEPTH=<D> MTG_DUMP_WINS=1 mtg --batch DIR/manifest.json
Fresh seeds 8900001 + 20000 * deck_index (no overlap with the clairvoyant A/B's 88xxxxx).
  python3 make_nc.py --out DIR --decks k1,k2 [--games 200]
  python3 make_dig_ab.py --analyse DIR   (job names reuse the d3 cell tag)
"""
import argparse, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_dig_ab as M

ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
ap.add_argument("--decks", required=True)
ap.add_argument("--games", type=int, default=200)
ap.add_argument("--seed0", type=int, default=8900001)
a = ap.parse_args()
jobs = []
for key, deck, prof in M.ALL_DECKS:
    if key not in a.decks.split(","):
        continue
    base = M.seed_base(a.seed0, key, "d3")
    for c in range(0, a.games, 25):
        for arm, fl in M.ARMS:
            j = {"name": "%s_d3_%s.c%d" % (key, arm, c), "deck": os.path.join(M.PRIMARY, deck),
                 "profile": os.path.join(M.PRIMARY, prof), "games": min(25, a.games - c), "seed": base + c,
                 "game_index": c, "budget_ms": 10, "depth": 3, "ignore_play_profile": True}
            if fl:
                j["flags"] = dict(fl)
            jobs.append(j)
os.makedirs(a.out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
json.dump({"seed0": a.seed0}, open(os.path.join(a.out, "seeds.json"), "w"))
print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))
