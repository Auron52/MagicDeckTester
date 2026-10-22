"""ONE pooled batch: MTG_PAY_DEPLETION_HOLD off vs on, every deck that holds a depletion land, PAIRED
(same seed + game count per cell, so game index i is the same deal in both arms) on HELD-OUT seeds
(500000+ for d3, 600000+ for the play cell -- disjoint from every suite seed range, 1001..12009 and
30001..30420, from the Niv A/B seeds 63000..66xxx, and from the agent's perf probe, 430000 / 440000).

Cells per deck, copied from test/regression_cases.sh's shape:
  d3  = depth 3, budget 10, ignore_play_profile (the suite's pinned coverage cell)
  play = no depth key, budget 20 (the deck's value_play block decides the depth -- the suite's d5 row)
NivMizzetCombo is not a suite deck yet; it gets the same two cells (its own profile).

Sized from the probe (2026-10-22, the same two cells at seeds 430000/440000, this build, alone on the box):
per-game core-ms th 98/127, dragonstorm 84/64, mirrorwing 128/108, galadriel 73/76, fungus 77/81,
angels 50/37, critter 74/26, whiteknights 30/7, breaching 3/1, kitty 310/542, bruna 789/2070,
niv 3311/3496 (d3/play). The probe moved play only on th (8% of games), galadriel (4%), niv (12%),
mirrorwing, dragonstorm and kitty, so those carry the games; the six that never moved keep 10000 per
cell for the per-deck bar. ~36.8k core-s per arm, ~73.6k in all -> ~61 min at 20 threads saturated,
~65-75 min with the tail. Paths are relative to the worktree (run with env -C).
usage: make_manifest.py <out.json> [scale]   (run_ab.sh is the one entry point)"""
import json
import sys

DECKS = {
    # key: (decklist, profile, d3 games, play games)
    "niv":          ("decks/NivMizzetCombo/NivMizzetCombo.cod", "decks/NivMizzetCombo/NivMizzetCombo.profile.json", 1500, 1000),
    "bruna":        ("decks/Bruna/Bruna.cod", "decks/Bruna/Bruna.profile.json", 2000, 1200),
    "th":           ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json", 25000, 20000),
    "dragonstorm":  ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json", 25000, 20000),
    "mirrorwing":   ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod", "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json", 20000, 20000),
    "breaching":    ("decks/BreachingDragonstorm/BreachingDragonstorm.cod", "decks/BreachingDragonstorm/BreachingDragonstorm.profile.json", 10000, 10000),
    "galadriel":    ("decks/Galadriel/Galadriel.cod", "decks/Galadriel/Galadriel.profile.json", 25000, 20000),
    "fungusv3":     ("decks/Fungus/Fungus.cod", "decks/Fungus/Fungus.profile.json", 10000, 10000),
    "angels":       ("decks/Angels/Angels.cod", "decks/Angels/Angels.profile.json", 10000, 10000),
    "critter":      ("decks/CritterLifegain/CritterLifegain.cod", "decks/CritterLifegain/CritterLifegain.profile.json", 10000, 10000),
    "kittyv3":      ("decks/KittyEquipment/KittyEquipment.cod", "decks/KittyEquipment/KittyEquipment.profile.json", 6000, 4000),
    "whiteknights": ("decks/WhiteKnights/WhiteKnights.cod", "decks/WhiteKnights/WhiteKnights.profile.json", 10000, 10000),
}
SEED_D3, SEED_PLAY = 500000, 600000
ARMS = {"off": {"MTG_PAY_DEPLETION_HOLD": False}, "on": {"MTG_PAY_DEPLETION_HOLD": True}}


def main():
    out = sys.argv[1]
    scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    jobs = []
    for key, (deck, prof, g3, gp) in DECKS.items():
        g3 = max(1, int(round(g3 * scale)))
        gp = max(1, int(round(gp * scale)))
        assert g3 <= 100000 and gp <= 100000, "a cell past 100000 games would run into the next seed block"
        for arm, flags in ARMS.items():
            jobs.append({"name": "dh_%s_%s_d3_s%d" % (key, arm, SEED_D3), "deck": deck, "profile": prof,
                         "games": g3, "seed": SEED_D3, "depth": 3, "budget_ms": 10,
                         "ignore_play_profile": True, "flags": dict(flags)})
            jobs.append({"name": "dh_%s_%s_play_s%d" % (key, arm, SEED_PLAY), "deck": deck, "profile": prof,
                         "games": gp, "seed": SEED_PLAY, "budget_ms": 20, "flags": dict(flags)})
    json.dump({"jobs": jobs}, open(out, "w"), indent=1)
    print("%d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))


if __name__ == "__main__":
    main()
