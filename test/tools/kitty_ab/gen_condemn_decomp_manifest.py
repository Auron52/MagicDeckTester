#!/usr/bin/env python3
"""THE CONDEMNATION WORK DECOMPOSITION -- the control arms ordv4 was missing.

WHY THIS RUN EXISTS. ordv4 measured cond_ng (ORD + NO_GREEDY_CONT + SITE3 + CLASSIFY) against a bare
baseline and read a mean per-game cost ratio of 1.59, i.e. condemnation appearing to make the typical
Hinata game 59% DEARER. That reading cannot be attributed, because cond_ng differs from base in FOUR
levers at once and two of them push cost in OPPOSITE directions:

  MTG_HINATA_ORDER_FULL   -- cast order; small cost effect
  MTG_BP_NO_GREEDY_CONT   -- deletes a GREEDY continuation, replacing it with SEARCH: costs MORE
  MTG_BP_SITE3            -- OPENS the plain-cantrip breakpoint class: costs MORE (measured ~1.51x
                             on its own, per the greedy-deletion arc), and is a prerequisite -- with
                             site 3 masked out NO_GREEDY_CONT cannot even reach Hinata's largest
                             greedy site
  MTG_BP_CLASSIFY         -- the condemnation filter itself: the only lever that should SAVE

"A lever spanning two call sites is TWO levers" is a rule this arc has already been burned by twice.
A four-lever bundle is four, and reading condemnation's cost off base -> cond_ng attributes the
site-3 opening to the prune that is supposed to pay for it.

THE ARMS BELOW ARE THE MISSING RUNGS OF THE LADDER, so every step isolates ONE lever:

  ord         (have)   cast order only
  ng                   + delete the greedy continuation      -> prices greedy deletion
  s3_ng                + open the plain-cantrip class        -> prices the site-3 OPENING (pure cost)
  cond_ng     (have)   + condemnation                        -> prices CONDEMNATION ALONE
  cond_ng_so           + restrict it to DECISION SPACE       -> prices the 30% leaf drops

and the same four again with MTG_HINATA_MANA_FLOAT_RANK, because that lever measurably recovered
most of the order's deficit (ordv4: the order costs 0.0225 on hold, the float fix recovers 0.0125 of
it) and condemnation must be priced in whichever order we would actually ship.

THE QUESTION THIS ANSWERS, in the USER's words (2026-08-28): "No matter what the result of
condemnation on Hinata it should reduce the overall work significantly. If it's not doing that, we
already have a bug." The number that answers it is units(cond_ng) / units(s3_ng) -- SAME site set,
SAME greedy policy, SAME order, condemnation the only difference. Nothing else in this arc does.

Prior evidence that it now has something to prune: with the type exemptions deleted and the revised
order live, the filter's drop rate went from 0.12% to 5.34% (43x) and its DECISION-SPACE drops from
122 to 60,549 (496x), with greedy_frac falling 0.90 -> 0.30. So the prune is firing; this run says
whether firing converts into work saved.

Pairs against ordv4's arms, so it MUST run on the SAME BINARY (build/Profile/mtg, unchanged since
10:32) and write into the SAME directory -- a game is seeded base_seed + game_index, so the report
pairs on the index intersection across runs.
"""
import json
import sys

H = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")

ORD   = {"MTG_HINATA_ORDER_FULL": True}
NG    = {"MTG_BP_NO_GREEDY_CONT": True}
S3    = {"MTG_BP_SITE3": True}
COND  = {"MTG_BP_CLASSIFY": True}
SO    = {"MTG_BP_CONDEMN_SEARCHED_ONLY": True}
FLOAT = {"MTG_HINATA_MANA_FLOAT_RANK": True}

# THE SEARCHED-ONLY ARMS, added after the telemetry above was read properly. Of the 86,225 drops,
# 25,676 (30%) still fire with g_search_candidate_enum FALSE -- the rollout leaf. A drop there
# prunes NOTHING (there is no plan-space branch to remove; the leaf is estimating, not deciding) and
# deletes the only line the playout was going to take, so it is pure quality loss at zero saving.
# That is the standing candidate cause of cond_ng's quality regression (-0.0525 on hold), and
# MTG_BP_CONDEMN_SEARCHED_ONLY is the gate that removes exactly those 30% while keeping the 70% that
# are genuine decision-space prunes. It is the same discriminator the MAIN-PHASE filter has shipped
# default-ON since 2026-08-21 -- whose own comment calls the ungated arm "the BROKEN arm".
ARMS = {
    "ng":                {**ORD, **NG},
    "s3_ng":             {**ORD, **NG, **S3},
    "cond_ng_so":        {**ORD, **NG, **S3, **COND, **SO},
    "s3_ng_float":       {**ORD, **NG, **S3, **FLOAT},
    "cond_ng_so_float":  {**ORD, **NG, **S3, **COND, **SO, **FLOAT},
}

# Same blocks and seeds as ordv4 -- that is what makes the pairing exact rather than approximate.
BLOCKS = {"train": 5500001, "hold": 6600001}
GAMES = 1200


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, flags in ARMS.items():
        for block, seed in BLOCKS.items():
            jobs.append({"name": f"{arm}.hinata.{block}", "deck": H[0], "profile": H[1],
                         "games": max(1, int(GAMES * scale)), "seed": seed, "flags": dict(flags)})
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
