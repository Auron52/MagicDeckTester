#!/usr/bin/env python3
"""Fungible-Equipment-copy odometer collapse (MTG_EQUIP_COPY_COLLAPSE) -- pooled A/B.

WHAT THE LEVER DOES. ActivationFamilyKey keys an Equip family on its SOURCE permanent, so N
identical Equipment permanents become N separate odometer groups and the walk spends 2^N positions
on them -- yet the copies are interchangeable, so those positions cover only N+1 distinct boards per
host. The collapse keeps one canonical (non-increasing-digits) position per equivalence class. See
BuildFungibleEquipClasses in src/ai/TurnSolver.cpp for the soundness argument and the refusals
(attached copies, copies carrying counters, Grafted Wargear).

WHY IT NEEDS MEASURING RATHER THAN ASSERTING. The collapse is a SYMMETRY argument, so the surviving
plan is equivalent -- but not identical: it can attach a different PHYSICAL copy than today's
tie-break picks, and card numbers feed downstream ordering. So play digests move, and on the single
pathological game (seed 9175 gi 166) the enumeration count moved with them (15,442 -> 18,735) even
though the subsets-considered total fell 43.7M -> 35.9M. One game cannot separate "equivalent plan,
different copy" from "a reachable plan was lost"; a paired sample over many seeds can, because a
lost plan has to cost turns somewhere.

ONE POOLED BATCH, NOT ONE PER ARM. The lever is a heurarm slot, so both arms ride the same manifest
and the runner keeps every core busy to a single tail. A batch per arm is the wave pattern CLAUDE.md
forbids -- it has starved this box twice.

SEED SPACING IS LOAD-BEARING: game identity is base+game_index, so bases must be spaced by at least
games-per-job or jobs replay each other's games. 500 games/job, bases 500 apart.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"
GAMES = 500
BASE = 700000

ARMS = {
    "base":     {},                                    # env default = off
    "collapse": {"MTG_EQUIP_COPY_COLLAPSE": True},
}


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 16
    jobs = []
    for i in range(n):
        seed = BASE + i * GAMES
        for arm, flags in ARMS.items():
            job = {
                "name":    f"{arm}.s{seed}",
                "deck":    DECK,
                "profile": PROF,
                "games":   GAMES,
                "seed":    seed,
            }
            if flags:
                job["flags"] = dict(flags)
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
