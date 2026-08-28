#!/usr/bin/env python3
"""DELETE THE GREEDY CONTINUATION, ADOPT THE REVIEWED ORDERS, ADOPT CONDEMNATION.
Suite-wide, ONE pooled queue, at PLAY settings, with DETERMINISTIC work units.

USER, 2026-08-28: "let's do what we can to remove greedy, adopt my orders and get condemnation
adopted. In addition to avoiding quality regressions, please ensure that we don't have notable
performance regressions ... neutral is also potentially acceptable if it means greedy is deleted,
though we should also make sure there are no unrecoverable issues."

So the bar per piece is: (1) quality neutral-or-better, (2) work units neutral-or-better, (3) no
unrecoverable lines. Adopt piece by piece where a piece clears all three on its own; otherwise adopt
the combination once the whole clears them.

WHAT THE GREEDY CENSUS FOUND (MTG_M2_YIELD_STATS, and it reshaped this run):

  * `MTG_BP_NO_GREEDY_CONT` does not merely REDUCE greedy -- on mirrorwing, kitty and th it deletes
    greedy DECISION-MAKING outright. The residual calls return EMPTY plans: the new `acted` column
    reads 0. Raw call counts said "58% reduction" and were misleading; volume is not harm, a fifth
    time. Site 90 (SolveWithLookahead's depth<=0 leaf) never decides a play on ANY deck, so it is
    not a greedy decision site at all.
  * IT IS A NO-OP ON HINATA WITHOUT SITE 3. Hinata's deferred continuation is the PLAIN-CANTRIP
    class, which BpSiteMask's 0x77 default masks out, so class_on is false and the no-greedy branch
    is never reached: 164,313 calls / 72,661 real decisions per 15 games, untouched. `MTG_BP_SITE3`
    opens it. With both, 14 of 15 decks reach ZERO greedy decisions.
  * FIVECOLOUR IS THE EXCEPTION and it is an ENUMERATION GAP, not greedy judgement: all 201
    fall-throughs are `empty-cands` (EnumerateBreakpointPlans returned nothing) yet greedy Solve
    finds a play in 16 of them. Reported, not papered over.

WHY SITE 3 IS ISOLATED AS ITS OWN ARM. It is a coverage change (a whole breakpoint class becomes
searchable) as well as the enabler for the greedy deletion, and a lever spanning two effects is two
levers -- that mistake has produced a wrong verdict twice in this arc.

THE ORDERS. `MTG_KE_ORDER_FULL` and `MTG_STOMPY_ORDER` (+ `MTG_TOP_RESOLVE`, the Stompy order's LOOP
half) are the USER's reviewed orders still shipping OFF. They are measured WITH the greedy deletion
as well as alone, because the no-greedy continuation picks `cands[0]` and that is only a principled
answer under a TOTAL order -- with a class ranking, rank ties leave cands[0] arbitrary among peers.
The orders are a prerequisite for the deletion, not an independent nicety.

CONDEMNATION rides the same queue on Mirrorwing (its only opt-in-able deck today), alone and with
the deletion.

PLAY SETTINGS throughout (depth/budget_ms omitted -> each deck's value_play block): an off-settings
diagnosis has lied by 700x in this repo. Run with MTG_DUMP_UNITS=1 so cost is compared in
GameWorkMeter units -- batch ms is WALL, and the same workload has measured 16.5 s and 48.9 s here
depending on load.
"""
import json
import sys

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
    "mirrorwing":      ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
                        "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
    "fivecolour":      ("decks/FiveColour/FiveColour.cod", "decks/FiveColour/FiveColour.profile.json"),
    "stompy":          ("decks/StompySurprise/StompySurprise.cod", "decks/StompySurprise/StompySurprise.profile.json"),
    "minotaur":        ("decks/Minotaur/Minotaur.cod", "decks/Minotaur/Minotaur.profile.json"),
    "kitty":           ("decks/KittyEquipment/KittyEquipment.cod", "decks/KittyEquipment/KittyEquipment.profile.json"),
}

NG   = {"MTG_BP_NO_GREEDY_CONT": True}
S3   = {"MTG_BP_SITE3": True, "MTG_BP_SITE3_DEFER": True}   # reachable, DEFERRED out of wave 0
COND = {"MTG_BP_CLASSIFY": True, "MTG_BP_CONDEMN_ORDER_AWARE": True}

# Every deck gets these four: the baseline, each axis alone, and the full greedy deletion.
GLOBAL_ARMS = {
    "base": {},
    "ng":   {**NG},
    "s3":   {**S3},
    "nogreedy": {**S3, **NG},          # the recipe that reaches zero greedy decisions
    # The EAGER site-3 form, kept as the record: measured +0.0228 (t=3.12) and 1.51x work units on
    # hinata, byte-identical everywhere else. That regression is what MTG_BP_SITE3_DEFER fixes, and
    # keeping the arm is how "the deferral is what helped" stays a measurement rather than a claim.
    "s3_eager":       {"MTG_BP_SITE3": True},
    "nogreedy_eager": {"MTG_BP_SITE3": True, **NG},
}

# ...and the per-deck reviewed orders / condemnation, alone and on top of the deletion.
DECK_ARMS = {
    "kitty": {
        "ord":          {"MTG_KE_ORDER_FULL": True},
        "ord_nogreedy": {"MTG_KE_ORDER_FULL": True, **S3, **NG},
        # KittyEquipment is the deck breakpoint condemnation was BUILT for (one breakpoint class,
        # the Puresteel equipment-ETB draw), and MTG_KE_CONDEMN is its provider opt-in -- still off.
        # Measured with the FULL order because condemnation enforces the declared order at
        # breakpoints, so a class ranking with ties is exactly where it has misfired before.
        "cond":          {"MTG_KE_CONDEMN": True},
        "cond_ord":      {"MTG_KE_CONDEMN": True, "MTG_KE_ORDER_FULL": True},
        "cond_nogreedy": {"MTG_KE_CONDEMN": True, "MTG_KE_ORDER_FULL": True, **S3, **NG},
    },
    "stompy": {
        "ord":          {"MTG_STOMPY_ORDER": True},
        "ord_top":      {"MTG_STOMPY_ORDER": True, "MTG_TOP_RESOLVE": True},
        "ord_nogreedy": {"MTG_STOMPY_ORDER": True, "MTG_TOP_RESOLVE": True, **S3, **NG},
    },
    "mirrorwing": {
        "cond":          {**COND},
        "cond_nogreedy": {**COND, **S3, **NG},
    },
}

# Disjoint blocks, spaced far wider than games-per-job (base seeds spaced under the job size REPLAY
# games; the tell is zero variance).
BLOCKS = {"train": 2200001, "hold": 3300001}
GAMES = 2500


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for deck, (path, prof) in DECKS.items():
        arms = dict(GLOBAL_ARMS)
        arms.update(DECK_ARMS.get(deck, {}))
        for arm, flags in arms.items():
            for block, seed in BLOCKS.items():
                job = {"name": f"{arm}.{deck}.{block}", "deck": path, "profile": prof,
                       "games": max(1, int(GAMES * scale)), "seed": seed}
                if flags:
                    job["flags"] = dict(flags)
                jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
