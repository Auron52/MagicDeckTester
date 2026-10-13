#!/usr/bin/env python3
"""Exactness gate for the dig fold: the same jobs at --budget-ms 0 (unbounded, beam off by default), run
under the pre-fold binary and the fold binary; every digest must match (a fold that removes only exact
duplicates cannot change an unbounded search's answer).
  python3 make_exact.py --out DIR [--games 30] [--depth 3]
  python3 make_ident.py --compare DIR_A DIR_B
"""
import argparse, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_dig_ab as M

ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
ap.add_argument("--games", type=int, default=30)
ap.add_argument("--depth", type=int, default=3)
ap.add_argument("--decks", default="fivecolour,pirates,soldiers,whiteknights,th,auras,fluctuator,dragons,edf")
a = ap.parse_args()
jobs = []
for key, deck, prof in M.ALL_DECKS:
    if key not in a.decks.split(","):
        continue
    n = 10 if key == "edf" else a.games
    jobs.append({"name": "%s_b0" % key, "deck": os.path.join(M.PRIMARY, deck),
                 "profile": os.path.join(M.PRIMARY, prof), "games": n, "seed": 8650001, "game_index": 0,
                 "budget_ms": 0, "depth": a.depth, "ignore_play_profile": True})
os.makedirs(a.out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
print("wrote %d jobs" % len(jobs))
