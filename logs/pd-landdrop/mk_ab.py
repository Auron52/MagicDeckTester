#!/usr/bin/env python3
# Paired held-out A/B manifest: PD and PD 2HG at play settings, arms innermost, alternating arm order per block.
import json, sys
FLAG = "MTG_PD_CASTLESS_LINE_NO_HOLD"
deck = "decks/Prevent Damage/Prevent Damage.cod"; prof = "decks/Prevent Damage/Prevent Damage.profile.json"
jobs = []
def add(tag, base_seed, n, extra):
    blk = 50
    for b, off in enumerate(range(0, n, blk)):
        arms = [("off", False), ("on", True)]
        if b % 2: arms.reverse()
        for arm, val in arms:
            j = {"name": f"{tag}_{arm}_{off}", "deck": deck, "profile": prof, "games": blk,
                 "seed": base_seed + off, "game_index": off, "flags": {FLAG: val}}
            j.update(extra)
            jobs.append(j)
add("pd", 9100001, int(sys.argv[1]), {})
add("pd2hg", 9200001, int(sys.argv[2]), {"starting_life": 30, "opponent_heads": 2})
json.dump({"jobs": jobs, "same_seed_inflight": 0}, open(sys.argv[3], "w"), indent=1)
print(len(jobs), "jobs")
