#!/usr/bin/env python3
"""KittyEquipment equip-doctrine sweep: fungible-copy odometer collapse x unsick last-resort host.

TWO LEVERS, MEASURED IN COMBINATION (the lesson from lever-sweeps-measure-in-combination: a lever
feeding a dead term read +0.0201 alone and -0.0616 in combination, so a single-bit screen settles
nothing).

  MTG_EQUIP_COPY_COLLAPSE  cost. N identical Equipment permanents become N odometer groups keyed on
                           sac_source_id, so the walk spends 2^N positions covering only N+1 boards.
                           Keeps one canonical position per fungible class. Symmetry, not dominance.
  MTG_EQUIP_UNSICK_HOST    quality. The consolidation doctrine's last-resort host (no Kemba and no
                           double striker on board) must be a creature that can attack THIS turn.
                           USER 2026-08-22: "if we have neither Kemba nor a doublestriker we should
                           put it on one creature with no summoning sickness."

They interact by construction: the collapse changes which equip positions the odometer offers, and
the host rule changes which host each offered equip targets, so `both` is the arm that matters for
adoption and the singles exist to attribute the delta.

ONE POOLED BATCH. Both levers are heurarm slots, so all four arms ride one manifest and the runner
keeps the cores busy to a single tail. A batch per arm is the wave pattern CLAUDE.md forbids.

SEED SPACING: game identity is base+game_index, so bases must be spaced by at least games-per-job or
jobs replay each other's games. 500 games/job, bases 500 apart.

PRIOR EVIDENCE (so the reader knows what this run is testing, not re-deriving): on 200 games at seed
9175 -- the range holding the known pathological game 166 -- the collapse produced a BYTE-IDENTICAL
play digest and identical avg (4.7150) for ~18% fewer subsets considered. This sweep asks whether
that holds at 8,000 games per arm, and what the host rule costs or buys on top.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"
GAMES = 500
BASE = 700000

COPY = "MTG_EQUIP_COPY_COLLAPSE"
SICK = "MTG_EQUIP_UNSICK_HOST"

ARMS = {
    "base":     {},
    "collapse": {COPY: True},
    "unsick":   {SICK: True},
    "both":     {COPY: True, SICK: True},
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
