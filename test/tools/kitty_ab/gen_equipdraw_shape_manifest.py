#!/usr/bin/env python3
"""Round 3: which SHAPE of breakpoint site 6 plays better -- deferred, or the partition?

USER design (2026-08-20/21): "we should only be considering spells that have not been considered
already at every point, making the breakpoints fully distinct from each other" / "the only way to do
this is through a mix of condemnation and only planning your section of the turn."

Two mechanisms, each a per-job flag, so all four combinations sweep in ONE pooled queue:

  MTG_EQUIP_DRAW_BP_INLINE  resolve the continuation AT the draw and truncate the base plan there
                            (the "plan only your section" half), instead of deferring it to after
                            every main cast.
  MTG_BP_CLASSIFY           condemn casts the section already considered (the "only new things"
                            half); the drawn card is exempt, being a duplicate of nothing.

WHY QUALITY HAS TO DECIDE THIS. The breadth probe already refuted the cost argument for the
partition -- inline RAISES total site-6 reaches (1.05M -> 2.01M on 8 games) because the partition
recurses and each continuation opens its own section, and condemnation cuts breadth about the same
either way (9.47 -> 4.56 deferred, 9.08 -> 5.02 inline). So the case for the partition rests on it
PLAYING better, not on it being cheaper, and that is what this measures.

Arms are paired on seed; `mc` is on everywhere except the two references because the metalcraft
credit is the pending shipped combination and these levers act on the same turns.
"""
import json
import sys

DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"

MC  = "MTG_METALCRAFT_CREDIT"
BP  = "MTG_EQUIP_DRAW_BP"
INL = "MTG_EQUIP_DRAW_BP_INLINE"
CL  = "MTG_BP_CLASSIFY"

ARMS = {
    "base":       {},                                   # shipped defaults, the paired baseline
    "mc":         {MC: True},                            # the credit alone
    "def":        {MC: True, BP: True},                  # site 6 as committed (deferred)
    "def_cl":     {MC: True, BP: True, CL: True},        # deferred + condemnation
    "inl":        {MC: True, BP: True, INL: True},       # partition, no condemnation
    "inl_cl":     {MC: True, BP: True, INL: True, CL: True},   # the full USER design
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
