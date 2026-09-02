#!/usr/bin/env python3
"""Build the Stage-1 (MTG_BP_NODE_D56) quality-gate manifest -- ONE pooled batch.

WHAT IS BEING MEASURED. MTG_BP_NODE hosts the plain-cantrip breakpoint (site 3) as a real search
node; MTG_BP_NODE_D56 extends that to the other two DEFERRED classes, site 5 (solo-target trick
with a draw/Treasure payload) and site 6 (Equipment cast under an ETB-draw watcher). Only two decks
hold those classes -- mirrorwing (5) and kitty (6) -- which the reachability census confirms: every
other deck's greedy census is identical across all arms.

ARMS. `base` is shipped. `node0` is MTG_BP_NODE + MTG_BP_NODE_D0ONLY, the standing adoption
candidate from this arc (greedy deletion for +3.8% units, quality -0.0065/-0.0059 at t 3.5/3.3).
`d56` and `d560` add Stage 1 on top of the full node and of that candidate respectively, so the
lever is isolated against BOTH the shipped baseline and the thing it would actually ship with.

DEPTH/BUDGET ARE OMITTED on purpose: ResolvePlaySettings then reads each deck's own value_play
lock, which is what "at play settings" means here. Seeds 5500001/6600001 are 1.1M apart, far beyond
games-per-job, so the two blocks cannot replay each other's games.

The 14 non-mover decks get a SCREEN block (node0 vs d560 only): the census says the lever cannot
touch them, and a matching play digest is the definitive proof of that, not the greedy counts.
"""
import json

DECKS = {
    "mirrorwing": ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
                   "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
    "kitty": ("decks/KittyEquipment/KittyEquipment.cod",
              "decks/KittyEquipment/KittyEquipment.profile.json"),
    "hinata": ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
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

ARMS = {
    "base": {},
    "node0": {"MTG_BP_NODE": True, "MTG_BP_NODE_D0ONLY": True},
    "d56": {"MTG_BP_NODE": True, "MTG_BP_NODE_D56": True},
    "d560": {"MTG_BP_NODE": True, "MTG_BP_NODE_D0ONLY": True, "MTG_BP_NODE_D56": True},
}

MOVERS = ["mirrorwing", "kitty"]
BLOCKS = [("train", 5500001), ("hold", 6600001)]
MOVER_GAMES = 5000
SCREEN_GAMES = 800

jobs = []
for deck in MOVERS:
    f, p = DECKS[deck]
    for arm, flags in ARMS.items():
        for blk, seed in BLOCKS:
            jobs.append(dict(deck=f, profile=p, games=MOVER_GAMES, seed=seed,
                             name=f"{deck}-{arm}.{blk}", flags=flags))

for deck, (f, p) in DECKS.items():
    if deck in MOVERS:
        continue
    for arm in ("node0", "d560"):
        jobs.append(dict(deck=f, profile=p, games=SCREEN_GAMES, seed=BLOCKS[0][1],
                         name=f"{deck}-{arm}.train", flags=ARMS[arm]))

# Longest first: the pool drains to a single tail, so the big mover jobs must start first.
jobs.sort(key=lambda j: -j["games"])
out = {"_note": __doc__, "jobs": jobs}
print(json.dumps(out, indent=1))
