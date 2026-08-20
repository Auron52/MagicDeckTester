#!/usr/bin/env python3
"""Round 2 of the equipment-ETB breakpoint screen: what does the WAVE-0 DEFERRAL cost in quality?

Round 1 (gen_equipdraw_manifest.py) established the class works and never loses -0.0267 held-out /
-0.0200 train, 7 games faster and 0 slower over 300 -- and that the eager form costs ~1.9x the search
work per game, because site 6 fires on almost every plan this deck has and wave 0 fans each one into
W=2 variants.

MTG_EQUIP_DRAW_BP_DEFER drops site 6 from wave 0 ONLY. It is a re-ordering, not a prune: the
deferred wave phase is built off the full BpSiteMask and picks every dropped plan up at rank 0.
So the question this measures is narrow and answerable: does reaching those continuations LATER
(with leftover budget instead of eagerly) keep the win, and how much of the cost does it give back?

Five arms, ONE queue. `base` is re-run rather than reused from round 1 so every pairing here is
within a single binary and a single box state -- and, because the round-1 base numbers are known,
it doubles as a check that the interim log-reporter fix really was inert.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

MC = "MTG_METALCRAFT_CREDIT"
BP = "MTG_EQUIP_DRAW_BP"
DEF = "MTG_EQUIP_DRAW_BP_DEFER"

ARMS = {
    "base":      {},
    "mc":        {MC: True},
    "bpdef":     {BP: True, DEF: True},              # the class alone, deferred out of wave 0
    "mc_bp":     {MC: True, BP: True},               # round 1's eager combination, re-measured here
    "mc_bpdef":  {MC: True, BP: True, DEF: True},    # the combination adoption would ship
}

# name -> (seed, games, depth). d3 only: the d5 repro cells are the tail and round 1 owns them.
CELLS = {"train": (300001, 150, 3), "hold": (900001, 150, 3)}


def main():
    scale = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    jobs = []
    for arm, flags in ARMS.items():
        for cell, (seed, games, depth) in CELLS.items():
            job = {
                "name":    f"{arm}.{cell}",
                "deck":    DECK,
                "profile": PROF,
                "games":   games * scale,
                "seed":    seed,
                "depth":   depth,
            }
            if flags:
                job["flags"] = dict(flags)
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
