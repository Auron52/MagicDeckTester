#!/usr/bin/env python3
"""Emit ONE pooled manifest for the BREAKPOINT CONDEMNATION decision.

The question: can KittyEquipment's `CondemnsConsideredAtBreakpoint()` go back ON now that the
order-aware fix exists (MTG_BP_CONDEMN_ORDER_AWARE) and the reviewed cast order is adopted
(MTG_KE_ORDER, default ON since 663627d5)?

THREE arms, not two. The middle arm is the control that keeps the diagnosis honest: if
order-awareness is really what fixes condemnation, then condemnation WITHOUT it must still lose,
here, at HEAD. Asserting that is cheap; assuming it is how the first (wrong) root cause survived.

  base     condemnation OFF -- HEAD as shipped
  condemn  condemnation ON, order-awareness OFF -- the known-bad arm (measured 0 better : 6 worse)
  condaw   condemnation ON, order-awareness ON  -- the proposed flip

Re-measured rather than reused: the earlier 0/0 reading predates MTG_SF_PUT_BP going default ON
(642d4b10), which arms site 6 off a Stoneforge PUT as well as a cast. That strictly widens the set
of continuations condemnation can filter, so the two levers interact and the old number is stale.

Mode 2 (play settings) -- see docs/design/three-measurement-modes.md. Arms are PAIRED on seed: job i
of every arm is the same shuffle, so a difference is a pure play difference. Cost is read with
cost.py (deterministic GameWorkMeter units); wall clock cannot answer it on this box.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

ARMS = {
    "base":    {},
    "condemn": {"MTG_KE_CONDEMN": True},
    "condaw":  {"MTG_KE_CONDEMN": True, "MTG_BP_CONDEMN_ORDER_AWARE": True},
}

# TRAIN is the block prior KittyEquipment verdicts were measured on; HOLD is disjoint, so the
# winner has somewhere honest to be confirmed. Spacing is far wider than games-per-job, which is
# the seed-overlap trap (base seeds spaced < games-per-job silently REPLAY games).
BLOCKS = {"train": 300001, "hold": 900001}


def main():
    games = int(sys.argv[1]) if len(sys.argv) > 1 else 2500
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    budget = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            job = {
                "name":      f"{arm}.{block}",
                "deck":      DECK,
                "profile":   PROF,
                "games":     games,
                "seed":      seed,
                "depth":     depth,
                "budget_ms": budget,
            }
            if flags:
                job["flags"] = flags
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
