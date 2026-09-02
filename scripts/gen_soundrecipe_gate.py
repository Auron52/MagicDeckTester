#!/usr/bin/env python3
"""Build the SOUND-RECIPE quality-gate manifest -- ONE pooled batch (phase 2 of sound-NGC).

THE CANDIDATE: MTG_BP_SITE3 + MTG_BP_SITE3_DEFER + MTG_BP_CANON_CONT + MTG_BP_NODE +
MTG_BP_NODE_ROOTTURN -- the recorded ROOTTURN recipe (+3.1% units, hinata -0.0162/-0.0174 at
t 6.0/6.8, beat the FULL node at 1/16th its cost) with its one disqualifier fixed: the recipe's
ordered continuation was NGC (lossy -- "cast nothing" unreachable, uncharged recursive enumeration
on Dragonstorm). CANON replaces it: act-vs-pass from the greedy path's own post-land Solve, cast
from cands[0], stand-down inside derivations.

ARMS (hinata gets the full ladder -- the deck the class exists for):
  base     shipped
  canon    MTG_BP_CANON_CONT alone (ties to the phase-1 gate; isolates the fallback change)
  screcipe the sound recipe (the adoption candidate)
  ngcrec   the OLD recipe (NGC in place of canon) -- the recorded -0.016 reference, so the
           canon-vs-NGC quality delta inside the recipe is read directly, same seeds
MOVERS (burn/th/dragonstorm/mirrorwing/kitty -- every deck the recipe's SITE3 opening moved in the
smoke preview) run base vs screcipe only: the question there is "does the sound recipe leave them
alone / better", not the decomposition.

Same discipline as phase 1: per-deck value_play settings (depth/budget omitted), two disjoint seed
blocks, MTG_DUMP_WINS paired. ACCEPTANCE: screcipe holds ~recipe-level hinata quality (a real gain,
t >= 2 both blocks), movers neutral-or-better, no one-sided SLOW-GAME tail (dragonstorm!), units
delta ~ +3% (read via a separate MTG_ROLLOUT_STATS cell, not this batch).
"""
import json

DECKS = {
    "hinata": ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
    "burn": ("decks/burn/burn.txt", "decks/burn/burn.profile.json"),
    "th": ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    "dragonstorm": ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
    "mirrorwing": ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
                   "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
    "kitty": ("decks/KittyEquipment/KittyEquipment.cod",
              "decks/KittyEquipment/KittyEquipment.profile.json"),
}

RECIPE = {"MTG_BP_SITE3": True, "MTG_BP_SITE3_DEFER": True, "MTG_BP_NODE": True,
          "MTG_BP_NODE_ROOTTURN": True}
ARMS = {
    "base": {},
    "canon": {"MTG_BP_CANON_CONT": True},
    "screcipe": dict(RECIPE, MTG_BP_CANON_CONT=True),
    "ngcrec": dict(RECIPE, MTG_BP_NO_GREEDY_CONT=True),
}

BLOCKS = [("train", 5500001), ("hold", 6600001)]

jobs = []
for deck, (f, p) in DECKS.items():
    # hinata's base and canon cells are NOT re-run: the phase-1 gate (logs/ngc_sound/gate.err)
    # holds them at the SAME games/seeds (10000 x 5500001/6600001), and the engine is
    # deterministic -- cat the two .err files for the paired analysis instead of paying 40k
    # duplicate games.
    arms = ["screcipe", "ngcrec"] if deck == "hinata" else ["base", "screcipe"]
    games = 10000 if deck == "hinata" else 5000
    for arm in arms:
        for blk, seed in BLOCKS:
            jobs.append(dict(deck=f, profile=p, games=games, seed=seed,
                             name=f"{deck}-{arm}.{blk}", flags=ARMS[arm]))

jobs.sort(key=lambda j: -j["games"])
out = {"_note": __doc__, "jobs": jobs}
print(json.dumps(out, indent=1))
