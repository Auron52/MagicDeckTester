# Elves round-2 held-out A/B: ONE pooled batch, arms innermost, one game per job, paired by seed = 9610001+gi AND
# game_index = gi (FRESH: disjoint from 9700001 / 9750001 / 9790001 / 9800001 / 9900001). d5 b20 with the profile;
# 2HG = life 30 + 2 heads. Arms (all temporary heurarm selectors; see src/ai/HeuristicArm.h):
#   origin  MTG_ELVES_R2=0              -- origin as-is (flat kick cap 20, Wave priced 3, land floor < 4 lands)
#   band    tasks 1-3 (kick cap = starting life, Wave priced 6, land floor always), hold-back band
#   three   + MTG_ELVES_HOLDBACK_THREE  -- only top, top - h_normal, top - h_all (the price of the in-between counts)
#   two     + MTG_ELVES_HOLDBACK_TWO    -- only top and top - h_normal (the USER's "attackers or all-in")
#   twotap  + two + MTG_ELVES_TAP_ORDER -- the same with the attackers-preserving tap order on a sink turn
#   bandtap + band + MTG_ELVES_TAP_ORDER -- the tap order alone
import json, sys
out = sys.argv[1]; n_std = int(sys.argv[2]); n_2hg = int(sys.argv[3])
R2 = {"MTG_ELVES_R2": True}
arms = (("origin", {"MTG_ELVES_R2": False}),
        ("band", dict(R2)),
        ("three", dict(R2, MTG_ELVES_HOLDBACK_THREE=True)),
        ("two", dict(R2, MTG_ELVES_HOLDBACK_TWO=True)),
        ("twotap", dict(R2, MTG_ELVES_HOLDBACK_TWO=True, MTG_ELVES_TAP_ORDER=True)),
        ("bandtap", dict(R2, MTG_ELVES_TAP_ORDER=True)))
jobs = []
for gi in range(max(n_std, n_2hg)):
    for fmt, n in (("std", n_std), ("2hg", n_2hg)):
        if gi >= n: continue
        for tag, fl in arms:
            j = {"name": f"{fmt}_{tag}@{gi}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
                 "games": 1, "seed": 9610001 + gi, "game_index": gi, "depth": 5, "budget_ms": 20, "flags": dict(fl)}
            if fmt == "2hg":
                j["starting_life"] = 30; j["opponent_heads"] = 2
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
