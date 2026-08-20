#!/usr/bin/env python3
"""Emit ONE pooled manifest for the equipment-ETB draw breakpoint (MTG_EQUIP_DRAW_BP, site 6).

The defect: Puresteel Paladin's "whenever an Equipment you control enters, draw a card" fires in
both worlds, but no breakpoint armed, so the drawn card could never be cast in the phase that drew
it (0 of 150 logged games ever cast a card that was not already in hand at the start of the turn).
The lever arms the deferred re-solve; this measures what it is worth and what it costs.

FOUR ARMS, because the metalcraft credit (MTG_METALCRAFT_CREDIT, measured -0.1400/game held-out and
awaiting adoption) is the OTHER pending KittyEquipment lever and the two touch the same turns: the
credit is what makes a second/third Equipment castable, and this breakpoint is what lets the card
those Equipment DREW be spent. A single-bit screen of either one would read the combination wrong
(the lever-sweeps-measure-in-COMBINATION lesson: a lever feeding a dead term read +0.0201 alone and
-0.0616 in combination). All four are in ONE queue, per-job "flags" (src/ai/HeuristicArm.h) -- never
one batch per arm.

Cells, identical to the metalcraft screen so the two verdicts are comparable:
  train  seed 300001, d3, the block every prior KittyEquipment verdict was measured on.
  hold   seed 900001, d3, disjoint and untouched, so a winner has somewhere honest to be confirmed.
  repro  seed 70001, d5, the claude-play sweep's own set.

Arms are PAIRED on seed: job i of every arm is the same shuffle, so a difference is pure play.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

MC = "MTG_METALCRAFT_CREDIT"
BP = "MTG_EQUIP_DRAW_BP"

ARMS = {
    "base":    {},                          # shipped defaults
    "mc":      {MC: True},                   # the pending credit alone
    "bp":      {BP: True},                   # the new breakpoint class alone
    "mc_bp":   {MC: True, BP: True},         # both -- the arm adoption would actually ship
}

# name -> (seed, games, depth)
CELLS = {"train": (300001, 150, 3), "hold": (900001, 150, 3), "repro": (70001, 16, 5)}


def main():
    scale = int(sys.argv[1]) if len(sys.argv) > 1 else 1   # multiply the d3 blocks only
    jobs = []
    for arm, flags in ARMS.items():
        for cell, (seed, games, depth) in CELLS.items():
            job = {
                "name":    f"{arm}.{cell}",
                "deck":    DECK,
                "profile": PROF,
                "games":   games if cell == "repro" else games * scale,
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
