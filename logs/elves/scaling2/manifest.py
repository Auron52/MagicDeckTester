# Elves scaling-prune band REFINEMENT held-out A/B (2026-11-27): ONE pooled batch, arms innermost, one game per job,
# paired by seed = 9790001+gi AND game_index = gi (FRESH: disjoint from the 9700001 and 9750001 samples). d5 b20 with
# the profile; 2HG = life 30 + 2 heads. Arms: holdall = the branch default (every attack-capable mana creature, every
# count down to all held), lump = lump points only, sink = Wave rows free only the flat dorks, combo = both,
# full = prune off.
import json, sys
out = sys.argv[1]; n_std = int(sys.argv[2]); n_2hg = int(sys.argv[3]); base = int(sys.argv[4]) if len(sys.argv) > 4 else 9790001
P = "MTG_ELVES_SCALING_PRUNE"; L = "MTG_ELVES_SCAL_LUMP"; S = "MTG_ELVES_SCAL_SINKAWARE"
arms = (("holdall", {P: True, L: False, S: False}),
        ("lump", {P: True, L: True, S: False}),
        ("sink", {P: True, L: False, S: True}),
        ("combo", {P: True, L: True, S: True}),
        ("full", {P: False, L: False, S: False}))
jobs = []
for fmt, n in (("std", n_std), ("2hg", n_2hg)):
    for gi in range(n):
        for tag, fl in arms:
            j = {"name": f"{fmt}_{tag}@{gi}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
                 "games": 1, "seed": base + gi, "game_index": gi, "depth": 5, "budget_ms": 20, "flags": dict(fl)}
            if fmt == "2hg":
                j["starting_life"] = 30; j["opponent_heads"] = 2
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
