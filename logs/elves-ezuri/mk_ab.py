#!/usr/bin/env python3
"""Elves Ezuri pump-count A/B manifest: ONE pooled batch, arms innermost (every chunk emits all arms back to back).
usage: mk_ab.py <out.json>   env: BASE (seed base), N_STD, N_2HG, CHUNK, ARMS ("name:flag=v;flag=v,...")
Pairing: chunk seed = BASE + gi0, game_index = gi0 (identical across arms). d5 b20 = the suite's elves play rows."""
import json, os, sys
BASE = int(os.environ.get("BASE", 8200000))
N_STD, N_2HG = int(os.environ.get("N_STD", 1000)), int(os.environ.get("N_2HG", 300))
CHUNK = int(os.environ.get("CHUNK", 10))
ARMS = os.environ.get("ARMS", "off:MTG_ELVES_EZURI_COUNTS=0,ez:MTG_ELVES_EZURI_COUNTS=1,"
                      "ezU:MTG_ELVES_EZURI_COUNTS=1;MTG_ELVES_EZURI_UNGATED=1,full:MTG_ELVES_SCALING_PRUNE=0")
arms = []
for a in ARMS.split(","):
    name, fl = a.split(":", 1)
    arms.append((name, {k: v == "1" for k, v in (x.split("=") for x in fl.split(";"))}))
jobs = []
for key, n, extra in (("elves", N_STD, {}), ("elves2hg", N_2HG, {"starting_life": 30, "opponent_heads": 2})):
    for gi0 in range(0, n, CHUNK):
        g = min(CHUNK, n - gi0)
        for name, flags in arms:
            j = {"name": f"{key}|p20|{name}|{gi0}", "deck": "decks/Elves/Elves.cod",
                 "profile": "decks/Elves/Elves.profile.json", "games": g, "seed": BASE + gi0, "game_index": gi0,
                 "depth": 5, "budget_ms": 20, "flags": flags}
            j.update(extra)
            jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=0)
print(len(jobs), "jobs;", sum(j["games"] for j in jobs), "games ->", sys.argv[1])
