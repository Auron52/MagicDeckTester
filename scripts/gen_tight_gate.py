#!/usr/bin/env python3
"""Build the TIGHT-vs-FULL canon-scope quality-gate manifest -- ONE pooled batch.

THE QUESTION. The sound recipe's quiet-box wall failed the USER's <10-15% bar (hinata +30.6%,
ds +49.7% at canon-everywhere). Scoping canon out of plain rollout applies + the verdict memo cut
that to +18.7%/+14.2%; the TIGHT scope (MTG_BP_CANON_REC=0 -- canon at root/resume/capture only,
RECORDING rollout applies back to greedy) reaches hinata +6.1% / ds +13.3%. 91.5% of hinata's
canon fires are [rollout+rec], so tight deletes most of the cost -- IF the quality survives.
Whether those rec-rollout fires carry any of the recipe's hinata gain (-0.0126/-0.0146 at
canon-everywhere) is not answerable by reasoning; this gate measures it.

ARMS (hinata, 10k/block): off / screcipe (scoped+memo form, the wall +18.7% reference) /
tightrecipe. Movers (th/kitty/mirrorwing/dragonstorm, 5k/block): off vs tightrecipe -- the
full-recipe mover cells exist from gate2 but on an OLDER binary, and tight != full, so tight
needs its own mover neutrality.

ACCEPTANCE: tightrecipe holds a real hinata gain (t >= 2 both blocks, ideally ~recipe-level);
movers neutral-or-better. If tight loses the quality, the +12% gap between tight and scoped IS
load-bearing and the next lever is the incremental key, not more scoping.
Analysis: scripts/paired_wins.py tight_gate.err hinata-off (etc., per deck).
"""
import json

DECKS = {
    "hinata": ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
    "th": ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    "kitty": ("decks/KittyEquipment/KittyEquipment.cod",
              "decks/KittyEquipment/KittyEquipment.profile.json"),
    "mirrorwing": ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
                   "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
    "dragonstorm": ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
}

RECIPE = {"MTG_BP_SITE3": True, "MTG_BP_SITE3_DEFER": True, "MTG_BP_NODE": True,
          "MTG_BP_NODE_ROOTTURN": True, "MTG_BP_CANON_CONT": True}
OFF = {k: False for k in RECIPE}
ARMS = {
    "off": OFF,
    "screcipe": dict(RECIPE),
    "tightrecipe": dict(RECIPE, MTG_BP_CANON_REC=False),
}

BLOCKS = [("train", 5500001), ("hold", 6600001)]

jobs = []
for deck, (f, p) in DECKS.items():
    arms = ["off", "screcipe", "tightrecipe"] if deck == "hinata" else ["off", "tightrecipe"]
    games = 10000 if deck == "hinata" else 5000
    for arm in arms:
        for blk, seed in BLOCKS:
            jobs.append(dict(deck=f, profile=p, games=games, seed=seed,
                             name=f"{deck}-{arm}.{blk}", flags=ARMS[arm]))

jobs.sort(key=lambda j: -j["games"])
out = {"_note": __doc__, "jobs": jobs}
print(json.dumps(out, indent=1))
