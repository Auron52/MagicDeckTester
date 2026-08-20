#!/usr/bin/env python3
"""Emit ONE pooled manifest for the metalcraft enumeration-credit A/B (MTG_METALCRAFT_CREDIT).

Three cells per arm, all in a SINGLE queue (per-job "flags", src/ai/HeuristicArm.h), so the cheap
d3 blocks backfill cores while the expensive d5 reproducer drains its tail -- one tail, no waves.

  repro  seed 70001, d5, the claude-play sweep's own set. gi=13 and gi=14 are the two games an
         informed human won a turn faster than the search by flipping metalcraft ON mid-turn and
         then stacking Colossus Hammers for {0} -- the exact line this credit exists to offer.
  train  seed 300001, d3, the block every prior KittyEquipment verdict was measured on.
  hold   seed 900001, d3, disjoint and untouched, so a winner has somewhere honest to be confirmed.

Arms are PAIRED on seed: job i of every arm is the same shuffle, so a difference is pure play.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

ARMS = {"mc_off": {}, "mc_on": {"MTG_METALCRAFT_CREDIT": True}}

# name -> (seed, games, depth)
CELLS = {"repro": (70001, 16, 5), "train": (300001, 150, 3), "hold": (900001, 150, 3)}


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
                job["flags"] = flags
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
