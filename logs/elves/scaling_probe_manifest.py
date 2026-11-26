# Elves scaling-prune quick probe: ONE pooled batch, arms innermost (block of 10 games: prune, full).
import json, sys
out = sys.argv[1]
jobs = []
for off in range(0, 300, 10):
    for tag, v in (("prune", True), ("full", False)):
        jobs.append({"name": f"{tag}@{off}", "deck": "decks/Elves/Elves.cod", "games": 10,
                     "seed": 7007 + off, "game_index": off, "depth": 5, "budget_ms": 20,
                     "flags": {"MTG_ELVES_SCALING_PRUNE": v}})
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
