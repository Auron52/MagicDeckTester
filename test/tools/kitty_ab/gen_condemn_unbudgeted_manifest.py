#!/usr/bin/env python3
"""CONDEMNATION PRICED UNBUDGETED -- the only way to read what a PRUNE costs.

WHY EVERY EARLIER UNITS NUMBER IN THIS ARC IS VOID. ordv4 and the decomposition both ran at Hinata's
PLAY settings (depth=5, budget=20ms, from the value_play block), and reported condemnation at +0.01%
to +0.90% work -- "saves nothing". That is not a measurement of the prune. SearchBudget is
denominated in the SAME GameWorkMeter units cost.py reports, and the iterative-deepening start gate
decides whether to begin another pass out of the remaining budget, so a budgeted search never
RETURNS a saving: it reinvests it in more passes. Total units stay pinned near the allowance and
tail noise sets the sign.

The diagnostic that exposed it: turning condemnation on left interior_nodes FLAT (-0.3%) while
rollout calls rose 22% and solve-memo misses 9.4%. That is the freed budget buying more passes, not
a prune doing damage. (A cache-fragmentation hypothesis -- condemnation widens the breakpoint enum
cache key with the whole pre-draw hand -- was built and REFUTED: with MTG_NO_BP_ENUM_CACHE=1 the
call count still rises 38%.)

USER, 2026-08-26, on the identical mistake the first time: "I'm very suspicious of this finding. How
are you measuring this? How can it do anything but less work?" Correct from first principles, twice.

Measured at budget_ms=0 over 8 games, the sign flips and the doubt evaporates: -4.40% units, with
EVERY per-game ratio <= 1.0 and an identical digest (play unchanged, so it is pure saving). This run
puts a real sample under that.

THE ARMS, all unbudgeted at the deck's play depth:

  s3_ng       site 3 open, greedy continuation deleted, NO condemnation   -- the control
  cond_ng_so  + condemnation, decision-space gated (the SHIPPED default)  -- the real price
  cond_ng     + condemnation, leaking into the rollout (SEARCHED_ONLY=0)  -- prices the bug

cond_ng must set the flag FALSE explicitly: MTG_BP_CONDEMN_SEARCHED_ONLY now defaults ON, and
heurarm::Flag takes an explicit per-job false over an env-true default.

BUDGETED NUMBERS STILL MATTER -- they say what a user actually experiences -- but they answer "how
was the budget spent", not "what does this prune cost". Report both, and never read a prune's cost
off a budgeted run again.
"""
import json
import sys

H = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")

BASE  = {"MTG_HINATA_ORDER_FULL": True, "MTG_BP_NO_GREEDY_CONT": True, "MTG_BP_SITE3": True}
ARMS = {
    "s3_ng":      {**BASE},
    "cond_ng_so": {**BASE, "MTG_BP_CLASSIFY": True},
    "cond_ng":    {**BASE, "MTG_BP_CLASSIFY": True, "MTG_BP_CONDEMN_SEARCHED_ONLY": False},
}

BLOCKS = {"train": 5500001, "hold": 6600001}
GAMES = 1200
DEPTH = 5          # the deck's play depth; only the BUDGET is lifted
BUDGET_MS = 0      # unlimited -- the whole point


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            jobs.append({"name": f"{arm}.hinata.{block}", "deck": H[0], "profile": H[1],
                         "games": max(1, int(GAMES * scale)), "seed": seed,
                         "depth": DEPTH, "budget_ms": BUDGET_MS,
                         # Required: the value_play block would otherwise pin depth/budget back to
                         # the play policy and silently undo the whole measurement.
                         "ignore_play_profile": True,
                         "flags": dict(flags)})
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
