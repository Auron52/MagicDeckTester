#!/usr/bin/env python3
"""Emit ONE pooled manifest for the KittyEquipment lever battery.

Every arm x every seed block is a job in a SINGLE queue, which is the whole point: the levers are
per-job "flags" (src/ai/HeuristicArm.h) rather than process env, so the cheap arms backfill cores
while an expensive one drains its tail. One tail for the entire battery, no waves.

Arms are PAIRED on seed: job i of every arm is the same shuffle, so an avg difference is a pure
play difference, not a deck-order difference.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

# name -> per-job flag overrides (see src/ai/HeuristicArm.h). {} == HEAD as shipped:
# searched second main (the live per-deck flip), all three package levers OFF.
ARMS = {
    "base":   {},
    "greedy": {"MTG_NO_SEARCH_SECOND_MAIN": True},   # re-verify the live SearchesSecondMain flip
    "order":  {"MTG_KE_ORDER": True},
    "park":   {"MTG_KE_PARK": True},
    "nagi":   {"MTG_EQUIP_MINPOWER_LAST": True},
    # The package in COMBINATION. Levers are not additive -- one feeding a dead term has read
    # +0.0201 alone and -0.0616 in combo on this repo -- so the combined arm is not optional.
    "pkg":    {"MTG_KE_ORDER": True, "MTG_KE_PARK": True, "MTG_EQUIP_MINPOWER_LAST": True},
}

# TRAIN is the seed block every prior KittyEquipment verdict was measured on; HOLD is disjoint and
# untouched, so a winner has somewhere honest to be confirmed.
BLOCKS = {"train": 300001, "hold": 900001}


def main():
    games = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            job = {
                "name":  f"{arm}.{block}",
                "deck":  DECK,
                "profile": PROF,
                "games": games,
                "seed":  seed,
                "depth": depth,
            }
            if flags:
                job["flags"] = flags
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
