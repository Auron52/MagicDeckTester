#!/usr/bin/env python3
"""KittyEquipment value leaf vs no leaf, on 4x the seeds phase E used.

USER (2026-08-22): "Can you confirm whether the games are budget churn and how results look on a
larger set of seeds?"

Phase E measured 8 paired seeds x 1000 games and returned -0.00138 turns at t=-1.17 (5 seeds better,
3 worse) for 0.61x the search cost. That t does not separate the quality delta from zero, so the
question is whether the per-seed signs are noise or a real small effect.

BUDGET CHURN IS NOT AVAILABLE AS AN EXPLANATION HERE, and that is worth stating because it is the
usual suspect. The two arms are built by phase E as sibling directories that differ in exactly one
file:

  variants/kittyequipment/live/    deck + profile                      (no sidecar)
  variants/kittyequipment/staged/  deck + profile + KittyEquipment.value.json

The two profiles are byte-identical (diff is empty), the staged model carries `value_play = null` so
neither arm changes play depth, and the A/B jobs set NO `budget_ms` and NO `depth` -- they run off the
profile. Nothing here is wall-clock-budget-limited, so a difference cannot come from one arm getting
further inside a time budget than the other; both arms are deterministic and reproducible. What is
left is the leaf replacing the horizon rollout, plus escalation timing under the crossover rule.

SEED SPACING IS LOAD-BEARING. Game identity is base+game_index, so bases must be spaced by at least
games-per-job or jobs REPLAY each other's games -- phase E's own comment records that closer spacing
"once turned 1.3 sigma into a fake -14.4 sigma". 1000 games/job, bases 1000 apart, so each job tiles
its own slice exactly once.

Seeds 600000..607000 are phase E's original eight; 608000..631000 are fresh. Nothing was tuned on the
first eight (the leaf is fit to phase-A rows, not to these games), so pooling all 32 is legitimate --
but the reader gets the fresh 24 broken out separately anyway, because "the effect survives on seeds
that had no chance to influence it" is the claim worth making.
"""
import json
import sys

VROOT = "logs/vlq_kittyequipment/variants/kittyequipment"
GAMES = 1000
N_SEEDS = 32
BASE = 600000


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else N_SEEDS
    jobs = []
    for i in range(n):
        seed = BASE + i * GAMES
        for arm in ("live", "staged"):
            jobs.append({
                "name":    f"{arm}.s{seed}",
                "deck":    f"{VROOT}/{arm}/KittyEquipment.cod",
                "profile": f"{VROOT}/{arm}/KittyEquipment.profile.json",
                "games":   GAMES,
                "seed":    seed,
            })
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
