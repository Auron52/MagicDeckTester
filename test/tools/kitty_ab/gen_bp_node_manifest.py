#!/usr/bin/env python3
"""THE BREAKPOINT NODE (MTG_BP_NODE) vs the control -- the partition measurement.

WHY THIS RUN EXISTS. The plain-cantrip class (site 3) is a quality win unbudgeted and a loss at the
shipped 20 ms budget, and the crossover sits at 80 ms = 4x shipped (cantrip-class-affordability.md).
The USER's directive is to make the class workable at the LOW budget, by partitioning the turn at
the breakpoint rather than widening ranked waves: a plan now ENDS at its first plain cantrip, the
apply stops at the partition point, and the plan loop hosts the continuation as a real search node
-- the full drawn-card-aware list plus an explicit empty continuation, no width W, no wave phase
(see BpNodeEnabled in TurnSolver.cpp, commit-local).

ARMS. ng = the shipped control (full order + no-greedy-continuation). node = ng + MTG_BP_NODE (the
class opens THROUGH the node; MTG_BP_SITE3 is implied, wave-0/walker excluded for site 3).
nodem2 = node + every cast in the second main (MTG_HINATA_ALL_MAIN2 + MTG_MAIN2_DROP) -- the
hinata-all-second-main.md arm re-measured now that FSLineTail hosts breakpoints for real (its
blocker was precisely the missing continuation search in main 2).

WHAT TO READ. Paired per-game deltas per block (ordv_report.py over the .wins logs); the committed-
depth/units decomposition comes from a separate MTG_ROLLOUT_STATS probe, not this batch.
"""
import json
import sys

H = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")

BASE = {"MTG_HINATA_ORDER_FULL": True, "MTG_BP_NO_GREEDY_CONT": True}
ARMS = {
    "ng":     {**BASE},
    "node":   {**BASE, "MTG_BP_NODE": True},
    "nodem2": {**BASE, "MTG_BP_NODE": True,
               "MTG_HINATA_ALL_MAIN2": True, "MTG_MAIN2_DROP": True},
}
BLOCKS = {"train": 5500001, "hold": 6600001}
GAMES = 1200
DEPTH = 5
BUDGET = 20     # the shipped budget -- the whole point of the arc


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            jobs.append({"name": f"{arm}.hinata.{block}",
                         "deck": H[0], "profile": H[1],
                         "games": max(1, int(GAMES * scale)), "seed": seed,
                         "depth": DEPTH, "budget_ms": BUDGET,
                         # Required: the deck's value_play lock would pin depth/budget back to
                         # the play policy and silently undo the A/B.
                         "ignore_play_profile": True,
                         "flags": dict(flags)})
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
