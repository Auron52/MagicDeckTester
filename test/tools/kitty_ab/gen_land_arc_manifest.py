#!/usr/bin/env python3
"""THE LAND ARC, BOTH HALVES, IN ONE POOLED QUEUE.

Two independent questions came out of the same finding -- that the land drop is the one decision the
cast order had no opinion about -- so they are measured together rather than in two batches.
CLAUDE.md's pooling rule is the reason: separate pools never share threads, so the cheap arms cannot
backfill cores while an expensive arm drains its tail, and every batch boundary is a barrier gated by
one slowest game. (Splitting a run per-arm has starved this box twice.) The suite-wide half's slow
decks and the Mirrorwing half's bulk overlap into a single tail here.

HALF A -- MTG_BP_CONDEMN_LAND (Mirrorwing only). The drop becomes a SLOT in the cast order
(LandDropCastOrderRank = 0, "land drop can go first in this deck"), so passing it is a real decline
and a land already in hand may not be played in the continuation; a land the breakpoint DREW stays
exempt. It CONDEMNS MORE -- the opposite direction from every exemption in the bug 4-8 stack -- and it
is the first order-side rule of the USER's programme ("I don't want to exempt things from the prune.
Instead, I want to figure out an order that works reliably"). It is not marginal: 6,832 land
condemnations against 773 cast ones over 10 games.

  * The greedy axis is part of the question. Land condemnation lives in the ENUMERATION, so a
    continuation that falls through to the greedy TurnSolver::Solve escapes it entirely.
    MTG_BP_NO_GREEDY_CONT deletes that fallback; the paired arms say how much the greedy path absorbs.
  * MTG_BP_CONDEMN_TAIL_EXEMPT is the bug-8 EXEMPTION, default OFF since the USER's "fix the order,
    not the prune" steer. Measured here because the order rule is what it was meant to be replaced
    by: if the order rule makes the exemption redundant, the programme is working.

HALF B -- MTG_ROLLOUT_LAND_RANKER (suite-wide). SimulateLandPlay is a hand-rolled two-pass mirror of
GreedyLandChoiceIndex, the ranker the executor uses and the enumeration tiebreak predicts against.
It has drifted, and the biggest drift is that it has NO NOTION OF TAPPED-NESS: a rollout turn holding
an untapped basic and a tapped dual plays the dual and spends the turn short of mana. Measured with
MTG_LANDDROP_STATS, this ranker decides the committed drop exactly ONCE in ~1,300 plans over 12 decks
-- it is the LEAF ESTIMATOR'S PLAYOUT POLICY, which is precisely where the breakpoint fan-out found
its damage ("the greedy continuation's real damage is to the LEAF EVALUATOR").

  * BUILT-IN NEGATIVE CONTROL: burn's greedy drop is 100% FORCED (one legal land name), so it is
    structurally immune and must read exactly 0.0000. A nonzero delta there means the lever does
    something other than what it claims.

SCOPE FIX: Half A runs the SHIPPED Mirrorwing list. Every card-level number in the bug-8 round came
from decks/Mirrorwing Dragon/v1-twinflame-anger, the archived variant, which contains none of the
shipped deck's cards.

PLAY SETTINGS throughout (depth/budget_ms omitted -> BatchRunner takes each deck's value_play block):
the adoption bar is overall avg at the settings the deck ships, and an off-settings diagnosis has
lied by 700x in this repo.
"""
import json
import sys

MW = ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
      "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json")

DECKS = {
    "slivers":         ("decks/slivers_vial/slivers_vial.txt", "decks/slivers_vial/slivers_vial.profile.json"),
    "burn":            ("decks/burn/burn.txt", "decks/burn/burn.profile.json"),
    "th":              ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    "knights":         ("decks/Knights/Knights.cod", "decks/Knights/Knights.profile.json"),
    "antilife":        ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json"),
    "hinata":          ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
    "dragonstorm":     ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
    "auras":           ("decks/Auras/Auras.cod", "decks/Auras/Auras.profile.json"),
    "goblins":         ("decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"),
    "creature_giving": ("decks/Creature Giving/Creature Giving.cod",
                        "decks/Creature Giving/Creature Giving.profile.json"),
    "mirrorwing":      MW,
    "fivecolour":      ("decks/FiveColour/FiveColour.cod", "decks/FiveColour/FiveColour.profile.json"),
    "stompy":          ("decks/StompySurprise/StompySurprise.cod", "decks/StompySurprise/StompySurprise.profile.json"),
    "minotaur":        ("decks/Minotaur/Minotaur.cod", "decks/Minotaur/Minotaur.profile.json"),
    "kitty":           ("decks/KittyEquipment/KittyEquipment.cod", "decks/KittyEquipment/KittyEquipment.profile.json"),
}

COND     = {"MTG_BP_CLASSIFY": True, "MTG_BP_CONDEMN_ORDER_AWARE": True}
LAND     = {"MTG_BP_CONDEMN_LAND": True}
NOGREEDY = {"MTG_BP_NO_GREEDY_CONT": True}
TAIL     = {"MTG_BP_CONDEMN_TAIL_EXEMPT": True}

COND_ARMS = {
    "base":           {},                              # what Mirrorwing ships today
    "cond":           {**COND},                        # cast condemnation as it stands
    "cond_land":      {**COND, **LAND},
    "cond_ng":        {**COND, **NOGREEDY},
    "cond_land_ng":   {**COND, **LAND, **NOGREEDY},
    "cond_tail":      {**COND, **TAIL},
    "cond_land_tail": {**COND, **LAND, **TAIL},
    "ng":             {**NOGREEDY},
}
ROLL_ARMS = {"rbase": {}, "rshared": {"MTG_ROLLOUT_LAND_RANKER": True}}

# Two disjoint blocks so a winner is confirmed on seeds it was not chosen on. Spaced far wider than
# games-per-job: base seeds spaced UNDER the job size replay games (the tell is zero variance).
BLOCKS   = {"train": 500001, "hold": 1050001}
R_BLOCKS = {"train": 700001, "hold": 1400001}

COND_GAMES = 12000   # Mirrorwing, 8 arms x 2 blocks
ROLL_GAMES = 800     # per deck, 2 arms x 2 blocks, 15 decks


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in COND_ARMS.items():
        for block, seed in BLOCKS.items():
            job = {"name": f"A.{arm}.{block}", "deck": MW[0], "profile": MW[1],
                   "games": max(1, int(COND_GAMES * scale)), "seed": seed}
            if flags:
                job["flags"] = dict(flags)
            jobs.append(job)
    for deck, (path, prof) in DECKS.items():
        for arm, flags in ROLL_ARMS.items():
            for block, seed in R_BLOCKS.items():
                job = {"name": f"B.{arm}.{deck}.{block}", "deck": path, "profile": prof,
                       "games": max(1, int(ROLL_GAMES * scale)), "seed": seed}
                if flags:
                    job["flags"] = dict(flags)
                jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
