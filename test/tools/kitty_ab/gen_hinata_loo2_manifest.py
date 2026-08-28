#!/usr/bin/env python3
"""WHY DOES HINATA'S FULL ORDER LOSE? Round 3 -- attribute the UNEXPLAINED half. ONE pooled queue.

USER, 2026-08-28: "I don't care about the generic rank except in terms of diagnostics. We need to
figure out why the new one doesn't work."

WHERE ROUND 2 LEFT IT (docs/design/hinata-cast-order.md sec 7). Every order arm is worse than the
shipped generic tiering on both blocks: full order +0.0210 (t=4.86) hold / +0.0270 (t=6.06) train.
Leave-one-out attributes ABOUT HALF to one position -- Irencrag Feat pinned second-last -- and
reverting it recovers 0.0100 hold / 0.0157 train. **The other half was never attributed**, and the
doc names the untested suspect: generic TIES Ornithopter and Hinata at 10, this order splits them
10/11. MTG_HINATA_DORK_TIE exists for exactly that and has never been run.

THE IRENCRAG FINDING IS A RULES POINT, NOT A TUNING ONE, and it is worth stating separately because
it is the USER's own ruling that costs the most. Verified against cards.json rather than recalled:

    Irencrag Feat {1}{R}{R}{R} Sorcery -- "Add seven {R}. You can cast only one more spell this
    turn."  params: ritual_floating_mana 7, max_casts_after 1

"One more spell" is ANY one spell. The ruling "it can only be cast before Crackle" therefore
forecloses Irencrag -> Soulfire Eruption ({6}{R}{R}{R}) and Irencrag -> Magma Opus ({6}{U}{R}), and
with seven red floating Crackle only reaches X=1 (5 damage) while Soulfire is castable off the seven
plus two lands. That is a real line the pin deletes.

CONDEMNATION IS NOT THE ANSWER HERE AND THIS RUN DOES NOT PURSUE IT. Measured 2026-08-28, 400 games:
condemnation is BYTE-IDENTICAL to baseline on this deck (digest 426a9d78 either way), and still
byte-identical with the plain-cantrip class opened (85d70bab either way). Structural, and visible in
both orders: condemnation only fires on a card STRICTLY earlier than the site, generic ties eleven
cards at rank 20 so nothing is ever strictly earlier, and under the full order the only cards ahead
of the cantrips are Sol Ring (mana rock) and Gamble (tutor) -- both exempt by bugs 4 and 5.

THE ARMS. Single-factor leave-one-out off the FULL order, so each position is priced alone, plus the
two- and three-factor stacks for the positions that pay. Every LOO lever moves a tier BACK toward the
generic tiering, so "recovers X" means "that position was costing X".
"""
import json
import sys

H = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")
FULL = {"MTG_HINATA_ORDER_FULL": True}

ARMS = {
    # The reference points.
    "base":      {},                                   # shipped generic tiering
    "full":      {**FULL},                             # the USER's reviewed order, as authored
    # Single-factor LOO: each reverts ONE tier toward generic.
    "iren":      {**FULL, "MTG_HINATA_IREN_EARLY": True},    # Irencrag 22 -> 18 (recovers ~half)
    "dork":      {**FULL, "MTG_HINATA_DORK_TIE": True},      # dork+engine tied at 10 -- NEVER RUN
    "find":      {**FULL, "MTG_HINATA_FIND_LATE": True},     # tutor/cantrips/dig -> 20 (after her)
    "paytie":    {**FULL, "MTG_HINATA_PAY_TIE": True},       # payoffs tied at 20 (BLOCK revert)
    "gamble":    {**FULL, "MTG_HINATA_GAMBLE_LATE": True},   # tutor after the cantrips
    "pp":        {**FULL, "MTG_HINATA_PP_STRICT": True},     # Ponder before Preordain
    # Stacks: if the unexplained half is the dork/engine split, iren+dork should land near base.
    "iren_dork": {**FULL, "MTG_HINATA_IREN_EARLY": True, "MTG_HINATA_DORK_TIE": True},
    "iren_dork_find": {**FULL, "MTG_HINATA_IREN_EARLY": True, "MTG_HINATA_DORK_TIE": True,
                       "MTG_HINATA_FIND_LATE": True},
    # ...and the best-case stack carrying the greedy deletion too, since that is the standing goal.
    "iren_dork_ng": {**FULL, "MTG_HINATA_IREN_EARLY": True, "MTG_HINATA_DORK_TIE": True,
                     "MTG_BP_SITE3": True, "MTG_BP_SITE3_DEFER": True,
                     "MTG_BP_NO_GREEDY_CONT": True},
}

# Disjoint blocks, spaced far wider than games-per-job.
BLOCKS = {"train": 5500001, "hold": 6600001}
GAMES = 3000


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            job = {"name": f"{arm}.hinata.{block}", "deck": H[0], "profile": H[1],
                   "games": max(1, int(GAMES * scale)), "seed": seed}
            if flags:
                job["flags"] = dict(flags)
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
