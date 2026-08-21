#!/usr/bin/env python3
"""Is the cleanup-shed RANKING worth anything on KittyEquipment? Bound it before authoring one.

USER question (2026-08-21): the earlier "discard fires 0 times in 96 games" census was real-play
only, and the user named two ways search reaches the shed anyway -- mulligan GENERATION (which plays
out land-light keeps the shipped keep table would throw away) and any branch that DECLINES the turn's
land drop (`land_to_play` empty is an enumerated defer option, not just "no land available").

Both mechanisms check out. The census with MTG_SHED_STATS says which one actually bites here:

    normal play   real=0  rollout=145888 (low-land=74181)   50 games, d3
    every opener force-kept ("--force-mulligan 0:")
                  real=0  rollout=142962 (low-land=71967)   50 games, d3

So real play never sheds even when every opener is kept (23 lands, nothing above 2 MV -- the hand
empties before it can flood), and the generation half does not bite ON THIS DECK. The rollout half
bites hard: ~2900 sheds per game, half of them with under 4 lands out, each taking index 0 of the
ranking with NO search above it (SpellEffects.h: "the rollout's own cleanup has no search at all and
takes index 0, so a bad ranking biases every line the search scores").

Firing often is not the same as MATTERING, which is what this measures. MTG_SHED_WORST is a
deliberate anti-heuristic in the MTG_LACKEY_RANK=low tradition: the rollout sheds the LAST-ranked
candidate instead of the first. It brackets the axis -- the shipped rule vs the worst rule available.
If those play the same, no ranking can repay authoring it, and the right answer to "should we write a
Kitty discard heuristic" is no, with a number behind it.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

ARMS = {
    "base":  {},                          # shipped ranking (index 0)
    "worst": {"MTG_SHED_WORST": True},    # the bound: last-ranked candidate
}

CELLS = {"train": (300001, 150, 3), "hold": (900001, 150, 3)}


def main():
    scale = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    jobs = []
    for arm, flags in ARMS.items():
        for cell, (seed, games, depth) in CELLS.items():
            job = {
                "name":    f"{arm}.{cell}",
                "deck":    DECK,
                "profile": PROF,
                "games":   games * scale,
                "seed":    seed,
                "depth":   depth,
            }
            if flags:
                job["flags"] = dict(flags)
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
