#!/usr/bin/env python3
"""MTG_ATTACK_LORD_CAST re-test (2026-11-27): ONE pooled held-out manifest, arms innermost.

Shape follows the 2026-11-22 lpab A/B (seed 1810000; 19 lord decks x 4000 searched + 50k d0), on FRESH seeds:
  * every suite deck holding a creature IsLordPermanent() accepts (lorddecks.py -- the lever is inert without one)
  * d0 (depth 0, budget 0, ignore_play_profile), N_D0 games
  * play settings (no depth -> the deck's value_play block; budget = the suite's shipped play-row budget), N_PLAY
  * 2HG play rows where the suite has one (life 30, heads 2), N_2HG
Arms: off (flag false), on (flag true), null (flag false, a second copy; first NULL_* games only) -- the null
arm bounds the in-process cross-game noise. Pairing: chunk seed = BASE + gi0, game_index = gi0.
usage: mk_lordcast.py <out.json> [key ...]"""
import json, re, sys
sys.path.insert(0, "logs/lordcast")
BASE = 7340000
N_D0, N_PLAY, N_2HG = 50000, 4000, 1000
NULL_D0, NULL_PLAY = 10000, 500
CHUNK_D0 = 1000
LORDS = ("angels beasts boars elementals elves fungusv3 giants goblins kithkin knights merfolk merfolkturns minotaur "
         "pirates redelem selesnyav2 slivers soldiers spirits stompy vampires whiteknights").split()
# Expensive play cells (suite_cost.json worst case): fewer games, small chunks, high LPT weight.
PLAY_N = {"beasts": 1000}
HG_N = {"beasts": 200}
txt = open("test/regression_cases.sh").read()
def amap(name):
    blk = re.search(name + r"=\((.*?)\n\)", txt, re.S).group(1)
    return {k: v.strip('"') for k, v in re.findall(r'^\s*\[(\w+)\]=("?[^"\n]+"?)\s*$', blk, re.M)}
FILE, PROF = amap("DECK_FILE"), amap("DECK_PROF")
rows = re.findall(r'^\s*"(\w+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"', txt, re.M)
def play_budget(key):
    b = {int(bud) for (k, d, s, g, bud) in rows if k == key and d == "5" and bud != "0"}
    if len(b) > 1: sys.exit(f"{key}: several play budgets {b}")
    return b.pop() if b else None
cost = json.load(open("test/suite_cost.json"))["decks"]
keys = sys.argv[2:] or LORDS
jobs = []
def emit(key, shape, n, chunk, extra, weight, null_n):
    base = key[:-3] if key.endswith("2hg") else key
    for gi0 in range(0, n, chunk):
        g = min(chunk, n - gi0)
        arms = [("off", False), ("on", True)] + ([("null", False)] if gi0 < null_n else [])
        for arm, on in arms:
            j = {"name": f"{key}|{shape}|{arm}|{gi0}", "deck": FILE[base], "profile": PROF[base], "games": g,
                 "seed": BASE + gi0, "game_index": gi0, "flags": {"MTG_ATTACK_LORD_CAST": on}, "weight": weight}
            j.update(extra)
            jobs.append(j)
for key in keys:
    cpg = cost.get(key, {}).get("cost_per_game_ms", 500.0)
    emit(key, "d0", N_D0, CHUNK_D0, {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}, 0, NULL_D0)
    b = play_budget(key)
    if b is None: sys.exit(f"{key}: no play row")
    n = PLAY_N.get(key, N_PLAY)
    ch = max(2, min(50, int(20000 / cpg)))   # ~20 core-s per chunk
    emit(key, f"p{b}", n, ch, {"budget_ms": b}, max(1, int(cpg)), NULL_PLAY)
    hb = play_budget(key + "2hg")
    if hb is not None:
        n = HG_N.get(key, N_2HG)
        emit(key + "2hg", f"p{hb}", n, ch, {"budget_ms": hb, "starting_life": 30, "opponent_heads": 2},
             max(1, int(cpg)), 0)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
from collections import Counter
print(len(jobs), "jobs;", sum(j["games"] for j in jobs), "games ->", sys.argv[1])
