#!/usr/bin/env python3
"""The "GOLD RUSH POSITIVE" rule, measured against the over-broad mana-site rule it replaces.
ONE pooled batch.

WHAT IS WRONG TODAY. Condemnation bug 7 shipped as "a MANA-ADDING breakpoint site condemns
nothing" (MTG_BP_CONDEMN_MANA_SITE_EXEMPT, default ON), and its test for "mana-adding" is
`creates_treasures > 0`. USER, 2026-08-27: *"Gold Rush does not make mana when not."* Gold Rush is
{1}{G} for ONE Treasure -- pay 2, get 1 -- so a BARE cast is mana-NEGATIVE and the rule's premise
is simply false there. It only becomes an engine once a copy magnet is live, because a solo-target
trick aimed at the magnet is copied once per other creature and every copy mints its own Treasure.

MEASURED how over-broad (MTG_TREASURE_SITE_PROBE, 20 games, MTG_BP_CLASSIFY=1): of 16,294
treasure-site exemption tests, **15,863 (97.4%) are net = -1** -- a cast that made the pool WORSE
-- and only 431 (2.6%) are net >= 0. The shipped rule drops the prune on a false premise in 97 of
every 100 firings.

BUT VOLUME IS NOT HARM -- three times in this arc a fire count has predicted the wrong sign (the
tutor exemption removed 85% of Hinata's condemnations and ~none of the damage; the copy exemption
removed 0.7% of Mirrorwing's and was the entire gain; the magnet looked dominant by count and
isolated at t=-1.00). So 97.4% is a reason to MEASURE, not a verdict. The specific thing that must
be checked: the site rule was justified by recovering 9 of 10 unrecoverable lines, and if those
recoveries sit at net=-1 sites then this fix RE-BREAKS them and the real mechanism is something
else. The per-game worse-lists below are what answers it.

THE RULE APPLIES AT TWO CALL SITES, AND A LEVER SPANNING TWO CALL SITES IS TWO LEVERS:
  * MTG_BP_CONDEMN_TREASURE_SITE_POSITIVE -- the condemnation exemption fires only at net >= 0;
  * MTG_MW_GR_LADDER_POSITIVE -- Gold Rush's funding ladder (15 -> 13 -> 6) offers its EARLY rungs
    only at net >= 0. USER: "if it doesn't add mana or fix colours then we hold it. This is true
    for any point prior to 15."
The ladder half is expected NEAR-INERT and is measured anyway: MTG_ORDER_RANGE_PROBE over 40 games
shows the ideal order paying in 11,074 of 11,133 ladder entries, i.e. the early rungs are already
all-but-dead in play. Measuring it is how "inert" is told apart from "never fired" -- this arc has
already produced two vacuous nulls from unverified levers.

net >= 0 is the bar and it covers both halves of the USER's sentence: net > 0 ADDS mana, net == 0
still FIXES COLOURS (two Treasures for {1}{G} turns a green pip into two wild at no loss).

Mode 2 (play settings): depth/budget_ms omitted so BatchRunner takes the deck's value_play block.
"""
import json
import sys

MW = ("decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.cod",
      "decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.profile.json")
GAMES = 8000

COND     = {"MTG_BP_CLASSIFY": True, "MTG_BP_CONDEMN_ORDER_AWARE": True}
POSITIVE = {"MTG_BP_CONDEMN_TREASURE_SITE_POSITIVE": True}
NO_SITE  = {"MTG_BP_CONDEMN_MANA_SITE_EXEMPT": False}
LADDER   = {"MTG_MW_GR_LADDER_POSITIVE": True}
# BUG 8 (2026-08-27): condemnation infers "already considered and declined" from a static cast-order
# RANK, but the plan that opens a breakpoint is very often the cantrip ALONE -- nothing preceded it,
# so nothing was declined, and the rest of the turn is deliberately deferred to the continuation.
# MTG_BP_CONDEMN_TAIL_EXEMPT (built 2026-08-25 on KittyEquipment, never measured) is exactly that
# guard: skip condemnation when the plan has no pending cast still in hand.
TAIL     = {"MTG_BP_CONDEMN_TAIL_EXEMPT": True}

ARMS = {
    # The quality baseline: condemnation OFF, i.e. what Mirrorwing actually ships today.
    "base":       {},
    # The three treasure-site policies, all with condemnation ON.
    "shipped":    {**COND},                          # bug 7 as shipped: ANY minter exempts
    "positive":   {**COND, **POSITIVE},              # the USER's rule: only a NET-POSITIVE minter
    "nosite":     {**COND, **NO_SITE},               # no treasure-site exemption at all
    # The order half, isolated (no condemnation) and combined.
    "ladder":     {**LADDER},
    "pos_ladder": {**COND, **POSITIVE, **LADDER},
    # Bug 8 and its combinations.
    "tail":        {**COND, **TAIL},
    "tail_pos":    {**COND, **TAIL, **POSITIVE},
    "tail_nosite": {**COND, **TAIL, **NO_SITE},
    "ship_ladder": {**COND, **LADDER},
}

BLOCKS = {"train": 500001, "hold": 1050001}


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            job = {
                "name":    f"{arm}.{block}",
                "deck":    MW[0],
                "profile": MW[1],
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
