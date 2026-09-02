#!/usr/bin/env python3
"""Build the MTG_BP_CANON_CONT (sound-NGC) quality-gate manifest -- ONE pooled batch.

WHAT IS BEING MEASURED. MTG_BP_CANON_CONT is the greedy-deletion form built after both prior forms
were rejected for known reasons: ACT-vs-PASS at an unresolved breakpoint continuation is judged by
the greedy path's own Solve at its own post-land state (fixing NGC's lost "cast nothing" -- the
treasure_hunt regression), the cast picked on ACT is the canonical cands[0] (the deck's cast
order), and the lever stands down inside a derivation (g_bp_enum_depth > 0), which removes NGC's
uncharged recursive-enumeration cascade (96% of its Dragonstorm gi=2686 lookup volume; the 11.8x
tail wall).

GATE. Same shape as the 136k recipe gate this lever exists to answer: every suite deck, at its own
value_play play settings (depth/budget omitted -> ResolvePlaySettings reads the deck's lock), two
disjoint seed blocks (1.1M apart, beyond games-per-job), paired per-game via MTG_DUMP_WINS on the
batch process. ACCEPTANCE: quality-neutral-or-better overall, hinata keeps a real gain (NGC's only
quality was hinata -0.006; if canon loses it the lever has no upside), th's +0.0016 regression
GONE, and NO one-sided dragonstorm tail in the SLOW-GAME list.

MOVER decks (hinata/th/kitty/mirrorwing/dragonstorm -- the decks every prior form moved) get
5,000/block (hinata 10,000: the -0.006 effect needs it); the rest get 1,500/block as a
did-anything-move screen with real power against gross regressions.
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
    "canon": {"MTG_BP_CANON_CONT": True},
}

MOVERS = ["hinata", "th", "kitty", "mirrorwing", "dragonstorm"]
BLOCKS = [("train", 5500001), ("hold", 6600001)]
GAMES = {"hinata": 10000}          # every other mover 5000, screens 1500

jobs = []
for deck, (f, p) in DECKS.items():
    games = GAMES.get(deck, 5000 if deck in MOVERS else 1500)
    for arm, flags in ARMS.items():
        for blk, seed in BLOCKS:
            jobs.append(dict(deck=f, profile=p, games=games, seed=seed,
                             name=f"{deck}-{arm}.{blk}", flags=flags))

# Longest first: the pool drains to a single tail, so the big mover jobs must start first.
jobs.sort(key=lambda j: -j["games"])
out = {"_note": __doc__, "jobs": jobs}
print(json.dumps(out, indent=1))
