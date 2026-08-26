#!/usr/bin/env python3
"""LEAVE-ONE-OUT decomposition of Hinata's full cast order, ONE pooled batch.

Round 1 (gen_hinata_order_manifest.py) measured the full order WORSE than the generic tiering:
+0.0210 hold (t=4.86) / +0.0270 train (t=6.06), n=3000/cell, 15-21 faster vs 67-83 slower. The order
changes several independent things at once, so "the order is bad" is not a usable finding. Each arm
here reverts exactly ONE piece to its generic position, on the SAME seeds, so the regression can be
attributed rather than guessed at.

  peer     the full order as reviewed (round 1's regressing arm, re-run on this binary)
  iren18   Irencrag Feat back to 18, ahead of every payoff. SINGLE FACTOR, and the prime suspect:
           the USER's "second last, it can only be cast before Crackle" forecloses Irencrag ->
           Soulfire AND Irencrag -> Opus. Soulfire is {6}{R}{R}{R}; Irencrag floats seven {R}.
  find20   tutor + cantrips + dig back to 20, i.e. AFTER her. SINGLE FACTOR: prices the
           draws-before-deploys promotion that breakpoint condemnation would need.
  paytie   the three payoffs tied at 20 again. BLOCK revert, not single-factor -- "second last" is
           defined relative to an ordered payoff block, so this necessarily moves Irencrag to 18
           as well. Read it as "the whole payoff block back to generic".

Round 1 also settled two things that are NOT re-measured here:
  * MTG_HINATA_PP_STRICT was BYTE-IDENTICAL to the peer arm over 12,000 games -- Ponder and
    Preordain are both breakpoint sites, so the plan truncates between them and their relative rank
    never arbitrates. The USER's "try it both ways" is answered: it cannot matter.
  * MTG_BP_NO_GREEDY_CONT alone is NEUTRAL on this deck at n=3000 (+0.0007 t=0.41 hold,
    +0.0013 t=1.41 train). The 0-better:4-worse lean at n=400 did not reproduce.

Mode 2 (play settings): depth/budget_ms omitted so BatchRunner takes the deck's value_play block.
Same blocks as round 1 so the two rounds are directly comparable.
"""
import json
import sys

DECK = "decks/Hinata2/Hinata2.cod"
PROF = "decks/Hinata2/Hinata2.profile.json"
GAMES = 3000

FULL = {"MTG_HINATA_ORDER_FULL": True}

ARMS = {
    "base":   {},
    "peer":   FULL,
    "iren18": {**FULL, "MTG_HINATA_IREN_EARLY": True},
    "find20": {**FULL, "MTG_HINATA_FIND_LATE":  True},
    "paytie": {**FULL, "MTG_HINATA_PAY_TIE":    True},
}

BLOCKS = {"train": 500001, "hold": 1050001}


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            job = {
                "name":    f"{arm}.{block}",
                "deck":    DECK,
                "profile": PROF,
                "games":   max(1, int(GAMES * scale)),
                "seed":    seed,
            }
            if flags:
                job["flags"] = flags
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
