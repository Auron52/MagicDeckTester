#!/usr/bin/env python3
"""Emit ONE pooled manifest for MTG_M2_D0_SEARCHED -- the last greedy step inside the branching.

The question (USER, 2026-08-23: "decks must have NO GREEDY components in the search"): the interior
second main falls to greedy Solve() whenever `depth <= 0`, REGARDLESS of every per-deck hook, and
that branch-site depth is the iterative-deepening PASS INDEX -- so pass 0, the pass that commits the
overwhelming majority of decisions, prices every candidate greedily. Measured at HEAD, 20 games:

  deck            BRANCH searched   BRANCH d<=0 (greedy)   committed at pass 0
  Anti-Lifegain              0            2,929                 100%
  KittyEquipment             0            9,936                 100%
  FiveColour             8,825           29,961                  63%

So all three decks that have ADOPTED the searched interior m2 still take it greedily most or all of
the time. The lever runs those calls at depth 1 instead -- one ply, no compounding with the outer
pass, but the plan is CHOSEN by enumeration + playout rather than by Solve()'s ordering.

Only these three decks can move: the lever is gated on the deck already having opted in, so it
widens an adopted design rather than starting a new one everywhere.

Mode 2 (play settings) -- depth and budget_ms are DELIBERATELY OMITTED so BatchRunner takes each
deck's own value_play block (AL d5/b20, 5C d6/b20, Kitty d5/b20). Pinning them here is how a
gate-cell measurement gets mistaken for a play one; see docs/design/three-measurement-modes.md.

Arms are PAIRED on seed: job i of both arms is the same shuffle, so a difference is a pure play
difference. Blocks are disjoint from every prior arc's (300001/900001) and spaced far wider than
games-per-job, which is the seed-overlap trap.
"""
import json
import sys

DECKS = {
    # name: (deck, profile, games per cell)
    "al":    ("decks/Anti-Lifegain/Anti-Lifegain.cod",
              "decks/Anti-Lifegain/Anti-Lifegain.profile.json", 5000),
    "kitty": ("decks/KittyEquipment/KittyEquipment.cod",
              "decks/KittyEquipment/KittyEquipment.profile.json", 5000),
    "5c":    ("decks/FiveColour/FiveColour.cod",
              "decks/FiveColour/FiveColour.profile.json", 2000),
}

ARMS = {
    "base": {},
    "d0s":  {"MTG_M2_D0_SEARCHED": True},
}

BLOCKS = {"train": 400001, "hold": 950001}


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for deck, (path, prof, games) in DECKS.items():
        for arm, flags in ARMS.items():
            for block, seed in BLOCKS.items():
                job = {
                    "name":    f"{deck}.{arm}.{block}",
                    "deck":    path,
                    "profile": prof,
                    "games":   max(1, int(games * scale)),
                    "seed":    seed,
                }
                if flags:
                    job["flags"] = flags
                jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
