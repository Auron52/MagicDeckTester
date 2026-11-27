# Elves round-3 held-out A/B: ONE pooled batch, arms innermost, one game per job, the ROUND-2 seeds (9610001+gi,
# game_index gi), d5 b20 with the profile; 2HG = life 30 + 2 heads. Arms:
#   band        hold-back band, inherited tap order (MTG_ELVES_TAP_ORDER off)
#   bandtap     + MTG_ELVES_TAP_ORDER + MTG_ELVES_TAP_WHOLEPLAN -- the round-2 order (attackers on EVERY payment of a
#               sink plan); must reproduce round 2's bandtap digests
#   bandtapfix  + MTG_ELVES_TAP_ORDER -- round 3: scaling kept for the set-up casts, attackers from the sink's payment on
import json, sys
ARMS = (("band", {"MTG_ELVES_TAP_ORDER": False}),
        ("bandtap", {"MTG_ELVES_TAP_ORDER": True, "MTG_ELVES_TAP_WHOLEPLAN": True}),
        ("bandtapfix", {"MTG_ELVES_TAP_ORDER": True}))
if __name__ == "__main__":
    out = sys.argv[1]; n_std = int(sys.argv[2]); n_2hg = int(sys.argv[3])
    jobs = []
    for gi in range(max(n_std, n_2hg)):
        for fmt, n in (("std", n_std), ("2hg", n_2hg)):
            if gi >= n: continue
            for tag, fl in ARMS:
                j = {"name": f"{fmt}_{tag}@{gi}", "deck": "decks/Elves/Elves.cod",
                     "profile": "decks/Elves/Elves.profile.json", "games": 1, "seed": 9610001 + gi, "game_index": gi,
                     "depth": 5, "budget_ms": 20, "flags": dict(fl)}
                if fmt == "2hg":
                    j["starting_life"] = 30; j["opponent_heads"] = 2
                jobs.append(j)
    json.dump({"jobs": jobs}, open(out, "w"), indent=1)
    print(len(jobs), "jobs")
