#!/usr/bin/env python3
"""Hinata cast order x CONDEMNATION, ONE pooled batch (CLAUDE.md: one queue, one tail).

WHY THIS SUPERSEDES ROUND 1. Round 1 measured the full order with condemnation OFF, which is only
half the design -- USER, 2026-08-26: "The searched order + condemnation design is what typically
makes that possible." Hinata does not opt into CondemnsConsideredAtBreakpoint and MTG_BP_CLASSIFY
defaults OFF, so BpClassifyActive was false in every round-1 arm. The order was priced purely as a
re-sequencing, never as the enabler it exists to be.

Round 1's two settled results, NOT re-measured (except as controls):
  * MTG_BP_NO_GREEDY_CONT alone is NEUTRAL on this deck at n=3000 (+0.0007 t=0.41 hold,
    +0.0013 t=1.41 train). The 0-better:4-worse lean at n=400 did not reproduce -- open item 1 of
    bp-greedy-continuation-deletion.md is closed.
  * The full order alone REGRESSED: +0.0210 hold (t=4.86) / +0.0270 train (t=6.06).

PONDER/PREORDAIN, the USER's "let's try it both ways". Round 1 found `strict` BYTE-IDENTICAL to
`peer` over 12,000 games -- with condemnation off, the split is inert. A MTG_CONDEMN_WHO preflight
shows why it stops being inert: with the order on, the pair CONDEMN EACH OTHER (Preordain @ Ponder
340, Ponder @ Preordain 304 in six games). So the arms that actually decide the question are
peer-vs-strict UNDER ORDER-AWARE condemnation, where the >= tie-exemption is what keeps the
measured `Ponder, Preordain, Ponder, Preordain` line reachable.

ORDER-AWARE also switches on the MANA exemption (BpSlotIsAfterSite returns early for a mana source
only when order-aware is enabled). The preflight condemned Sol Ring 705 times without it -- exactly
the accelerant-nailed-to-its-rank failure that exemption was built for.

Mode 2 (play settings): depth/budget_ms omitted so BatchRunner takes the deck's value_play block.
Blocks match round 1's so the two rounds are directly comparable.
"""
import json
import sys

DECK = "decks/Hinata2/Hinata2.cod"
PROF = "decks/Hinata2/Hinata2.profile.json"
GAMES = 3000

FULL   = {"MTG_HINATA_ORDER_FULL": True}
COND   = {"MTG_BP_CLASSIFY": True}
OA     = {"MTG_BP_CONDEMN_ORDER_AWARE": True}
IREN   = {"MTG_HINATA_IREN_EARLY": True}

ARMS = {
    # --- order alone: finish round 1's leave-one-out attribution of the regression -------------
    "base":     {},
    "peer":     FULL,
    "iren18":   {**FULL, **IREN},                      # single factor: Irencrag back ahead of payoffs
    "find20":   {**FULL, "MTG_HINATA_FIND_LATE": True},# single factor: find tier back after her
    "paytie":   {**FULL, "MTG_HINATA_PAY_TIE": True},  # block revert: payoffs + restrictor generic
    # --- the design the order exists for -------------------------------------------------------
    "basecond": COND,                                  # CONTROL: condemnation on the tie-heavy order
    "peercond": {**FULL, **COND},                      # order + condemnation, not order-aware
    "peeroa":   {**FULL, **COND, **OA},                # + order-aware (peers exempt each other)
    "strictoa": {**FULL, **COND, **OA,                 # the USER's question, in the one config
                 "MTG_HINATA_PP_STRICT": True},        #   where it is not inert
    "irenoa":   {**FULL, **IREN, **COND, **OA},        # best-guess order fix + full design
    "irenoang": {**FULL, **IREN, **COND, **OA,         # ...and the greedy deletion on top
                 "MTG_BP_NO_GREEDY_CONT": True},
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
