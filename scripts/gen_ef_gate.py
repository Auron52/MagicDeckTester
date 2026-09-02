#!/usr/bin/env python3
"""Build the MTG_EXEC_FEAS-on-recipe quality-gate manifest -- ONE pooled batch.

WHAT IS BEING MEASURED. The EF probe (logs/ngc_sound/ef_probe.err, 2026-09-02 overnight) showed
MTG_EXEC_FEAS on top of the sound recipe is a small real hinata gain (-0.0017 hold t -3.27 /
-0.0016 train t -3.41) AND cheaper (units 11,389,594 -> 11,326,878 = -0.55%): executor-validated
sequential payability prunes infeasible subsets the recipe's node would otherwise expand. Before
EF can be its own adoption candidate it needs the same neutrality evidence the canon lever got:
every OTHER suite deck, base arm = the sound recipe (the intended shipped state), EF added on top.

HINATA IS DELIBERATELY ABSENT: the EF probe already holds its 10k/block x train+hold cells on the
SAME binary and seeds -- re-paying 40k games would buy nothing.

GATE. Same discipline as the canon phase-1 gate: per-deck value_play settings (depth/budget
omitted), two disjoint blocks 1.1M apart, MTG_DUMP_WINS paired on the batch process. ACCEPTANCE:
neutral-or-better on every deck (|t| < 2 or better-direction), no one-sided SLOW-GAME tail.
Analysis per deck: scripts/paired_wins.py ef_gate.err <deck>-screcipe
"""
import json

DECKS = {
    "mirrorwing": ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
                   "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
    "kitty": ("decks/KittyEquipment/KittyEquipment.cod",
              "decks/KittyEquipment/KittyEquipment.profile.json"),
    "th": ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    "auras": ("decks/Auras/Auras.cod", "decks/Auras/Auras.profile.json"),
    "burn": ("decks/burn/burn.txt", "decks/burn/burn.profile.json"),
    "dragonstorm": ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
    "antilife": ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json"),
    "creature_giving": ("decks/Creature Giving/Creature Giving.cod",
                        "decks/Creature Giving/Creature Giving.profile.json"),
    "knights": ("decks/Knights/Knights.cod", "decks/Knights/Knights.profile.json"),
    "goblins": ("decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"),
    "slivers": ("decks/slivers_vial/slivers_vial.txt", "decks/slivers_vial/slivers_vial.profile.json"),
    "fivecolour": ("decks/FiveColour/FiveColour.cod", "decks/FiveColour/FiveColour.profile.json"),
    "stompy": ("decks/StompySurprise/StompySurprise.cod", "decks/StompySurprise/StompySurprise.profile.json"),
    "minotaur": ("decks/Minotaur/Minotaur.cod", "decks/Minotaur/Minotaur.profile.json"),
    "dragons": ("decks/Dragons/Dragons.cod", "decks/Dragons/Dragons.profile.json"),
}

RECIPE = {"MTG_BP_SITE3": True, "MTG_BP_SITE3_DEFER": True, "MTG_BP_NODE": True,
          "MTG_BP_NODE_ROOTTURN": True, "MTG_BP_CANON_CONT": True}
ARMS = {
    "screcipe": dict(RECIPE),
    "efrecipe": dict(RECIPE, MTG_EXEC_FEAS=True),
}

MOVERS = ["th", "kitty", "mirrorwing", "dragonstorm"]
BLOCKS = [("train", 5500001), ("hold", 6600001)]

jobs = []
for deck, (f, p) in DECKS.items():
    games = 5000 if deck in MOVERS else 1500
    for arm, flags in ARMS.items():
        for blk, seed in BLOCKS:
            jobs.append(dict(deck=f, profile=p, games=games, seed=seed,
                             name=f"{deck}-{arm}.{blk}", flags=flags))

# Longest first: the pool drains to a single tail, so the big mover jobs must start first.
jobs.sort(key=lambda j: -j["games"])
out = {"_note": __doc__, "jobs": jobs}
print(json.dumps(out, indent=1))
