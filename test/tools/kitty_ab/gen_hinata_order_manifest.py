#!/usr/bin/env python3
"""Emit ONE pooled manifest for the Hinata FULL cast order, both Ponder/Preordain arms.

Two questions in one pooled batch (CLAUDE.md: one queue, one tail -- never a batch per arm):

  Q1  USER, 2026-08-26: "Let's try it both ways regarding ponder and preordain."
      Ponder and Preordain both manipulate the TOP of the library, so their relative order is
      state-dependent in a way the rest of the deck's draws are not (USER: "Only the 'I know what is
      on the top of the library' effects like Ponder and Preordain are troublesome here").
        peer   = both at rank 7, order left to the search (MTG_HINATA_ORDER_FULL)
        strict = Ponder 7, Preordain 8      (+ MTG_HINATA_PP_STRICT)
      MEASURED before building this: over 120 games at play settings, 12 turns cast more than one
      top-manipulator and one is exactly `Ponder, Preordain, Ponder, Preordain` -- a line `strict`
      cannot express (it forces Ponder, Ponder, Preordain, Preordain). So the prior is that `peer`
      is at worst neutral and `strict` risks a rare reachability loss; this measures whether that
      shows up in the metric.

  Q2  Does the full order fix Hinata's lean under MTG_BP_NO_GREEDY_CONT? Hinata is the ONE deck
      leaning negative under the greedy-continuation deletion (0 better : 4 worse, n=400, t=1.42 --
      open item 1 of docs/design/bp-greedy-continuation-deletion.md). The lever casts cands[0], the
      CANONICAL continuation, instead of a greedy Solve -- and with seven live cards tied on rank 20
      "canonical" was decided by enumeration order, not by judgement. If that is the cause, the
      order should erase the lean.

The 2x3 cross answers both and prices the interaction, which a pair of separate A/Bs cannot.

Mode 2 (play settings): depth and budget_ms are DELIBERATELY OMITTED so BatchRunner takes the deck's
own value_play block. Pinning them is how a gate-cell measurement gets mistaken for a play one; see
docs/design/three-measurement-modes.md.

Arms are PAIRED on seed: job i of every arm is the same shuffle, so a difference is a pure play
difference. Blocks are disjoint from every prior arc's (300001/900001, 400001/950001, and the
harness's 1001/2002/3003) and spaced far wider than games-per-job -- the seed-overlap trap.

n=3000/cell clears open item 1's ">= 2000 games/block before any default flip".
"""
import json
import sys

DECK = "decks/Hinata2/Hinata2.cod"
PROF = "decks/Hinata2/Hinata2.profile.json"
GAMES = 3000

FULL   = {"MTG_HINATA_ORDER_FULL": True}
STRICT = {"MTG_HINATA_ORDER_FULL": True, "MTG_HINATA_PP_STRICT": True}
NG     = {"MTG_BP_NO_GREEDY_CONT": True}

ARMS = {
    "base":     {},                    # shipped: generic tiering, greedy continuation
    "peer":     FULL,                  # full order, Ponder/Preordain peers
    "strict":   STRICT,                # full order, Ponder before Preordain
    "ng":       NG,                    # greedy deletion alone (reproduces the -4 lean)
    "peerng":   {**FULL,   **NG},      # the candidate pairing
    "strictng": {**STRICT, **NG},
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
