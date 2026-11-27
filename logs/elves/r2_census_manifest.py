# Diagnostic census batches for the round-2 hold-back question (NOT part of the A/B: MTG_ELVES_CHOSEN_CENSUS
# re-enumerates at the real state and may warm caches). Same seeds as the A/B (9610001+gi, gi < n). Usage:
#   r2_census_manifest.py out.json n fmt arm[,arm...]      fmt = std | 2hg | both
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
out, n, fmt, arms_s = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
R2 = {"MTG_ELVES_R2": True}
ARMS = {"band": dict(R2), "twotap": dict(R2, MTG_ELVES_HOLDBACK_TWO=True, MTG_ELVES_TAP_ORDER=True),
        "bandtap": dict(R2, MTG_ELVES_TAP_ORDER=True), "three": dict(R2, MTG_ELVES_HOLDBACK_THREE=True)}
fmts = ("std", "2hg") if fmt == "both" else (fmt,)
jobs = []
for gi in range(n):
    for f in fmts:
        for tag in arms_s.split(","):
            j = {"name": f"{f}_{tag}@{gi}", "deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json",
                 "games": 1, "seed": 9610001 + gi, "game_index": gi, "depth": 5, "budget_ms": 20, "flags": dict(ARMS[tag])}
            if f == "2hg":
                j["starting_life"] = 30; j["opponent_heads"] = 2
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
