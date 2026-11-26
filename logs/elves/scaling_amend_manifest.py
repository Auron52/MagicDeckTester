# Elves scaling-prune AMENDMENT held-out A/B: ONE pooled batch, arms innermost, one game per job (paired by seed/gi).
# FRESH seeds 9750001+gi (disjoint from the 9700001 verdict that found the 9 slower games); d5 b20 with the profile.
# Arms: cur = the first-cut prune (1-mana band), amend = the amended band (two largest attack-capable mana creatures),
# holdall = the amended band freeing EVERY attack-capable mana creature, full = prune off.
import json, sys
out = sys.argv[1]; n_std = int(sys.argv[2]); n_2hg = int(sys.argv[3])
arms = (("cur", {"MTG_ELVES_SCALING_PRUNE": True, "MTG_ELVES_SCALING_AMEND": False}),
        ("amend", {"MTG_ELVES_SCALING_PRUNE": True, "MTG_ELVES_SCALING_AMEND": True, "MTG_ELVES_SCALING_HOLD_ALL": False}),
        ("holdall", {"MTG_ELVES_SCALING_PRUNE": True, "MTG_ELVES_SCALING_AMEND": True, "MTG_ELVES_SCALING_HOLD_ALL": True}),
        ("full", {"MTG_ELVES_SCALING_PRUNE": False}))
jobs = []
for fmt, n in (("std", n_std), ("2hg", n_2hg)):
    for gi in range(n):
        for tag, fl in arms:
            j = {"name": f"{fmt}_{tag}@{gi}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
                 "games": 1, "seed": 9750001 + gi, "game_index": gi, "depth": 5, "budget_ms": 20, "flags": dict(fl)}
            if fmt == "2hg":
                j["starting_life"] = 30; j["opponent_heads"] = 2
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
