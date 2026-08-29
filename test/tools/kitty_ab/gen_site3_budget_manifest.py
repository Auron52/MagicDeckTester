#!/usr/bin/env python3
"""SITE 3 vs THE BUDGET -- the crossover sweep.

WHY THIS RUN EXISTS. The plain-cantrip breakpoint class (site 3) is a quality WIN at equal depth
(unbudgeted d5: hold +0.0492 t=+6.59, train +0.0350 t=+5.31) and a LOSS at the shipped 20 ms budget
(-0.0233 / -0.0308). Seven attempts to make it "cheaper" each returned ~1% or nothing, because the
premise was wrong: measured per-site (MTG_ROLLOUT_STATS units.* buckets, 2026-08-29), the two arms
spend essentially the SAME units at 20 ms -- 16.08M vs 16.22M over 300 games, +0.9%. The class does
not cost more at play. It spends the same budget WORSE:

    committed iterative-deepening depth, 300 games at d5/20ms
      ng   n=772  mean 3.233   depth1 10.2%   depth5 22.1%
      s3   n=857  mean 2.819   depth1 19.7%   depth5 15.3%

-0.41 plies. The class buys breadth at the breakpoint and pays for it in depth, and on Hinata depth
is worth more. So the question is not "how do we prune it" but "what budget does it need", and that
is a crossover this sweep locates: at infinite budget the class is ahead by +0.049, at 20 ms it is
behind by -0.023, so a crossover budget EXISTS between them. Its value is the price of the class.

WHAT TO READ. Per budget, the paired s3-minus-ng delta (ordv_report.py). Three outcomes, all
decision-ready: (a) the crossover is near 20 ms -> the class is nearly free and a small budget bump
adopts it; (b) it is at 4-8x -> the class is real but expensive, and the USER decides; (c) there is
no crossover below the unbudgeted limit -> depth dominates breadth on this deck and the class should
stay closed at play settings regardless of how cheap it gets.

BUDGETS ARE VIRTUAL MS (SearchBudget::NODES_PER_VIRTUAL_MS = 900 units/ms), so this sweep is
deterministic and machine-independent like every other measurement here.
"""
import json
import sys

H = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")

BASE = {"MTG_HINATA_ORDER_FULL": True, "MTG_BP_NO_GREEDY_CONT": True}
ARMS = {
    "ng": {**BASE},                            # site 3 closed -- the control
    "s3": {**BASE, "MTG_BP_SITE3": True},      # site 3 open -- the class
}
BUDGETS = [20, 40, 80, 160]     # 20 = shipped; unbudgeted is already measured (s3q)
BLOCKS = {"train": 5500001, "hold": 6600001}
GAMES = 1200
DEPTH = 5


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for b in BUDGETS:
            for block, seed in BLOCKS.items():
                jobs.append({"name": f"{arm}b{b}.hinata.{block}",
                             "deck": H[0], "profile": H[1],
                             "games": max(1, int(GAMES * scale)), "seed": seed,
                             "depth": DEPTH, "budget_ms": b,
                             # Required: a deck's value_play lock would pin depth/budget back to the
                             # play policy and silently undo the whole sweep.
                             "ignore_play_profile": True,
                             "flags": dict(flags)})
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
