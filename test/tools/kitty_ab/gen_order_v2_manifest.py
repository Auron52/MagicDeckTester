#!/usr/bin/env python3
"""THE REVISED HINATA ORDER + NO TYPE EXEMPTIONS. One pooled queue, play settings, deterministic units.

USER 2026-08-28, in sequence: "Sol Ring should rank before Izzet Signet, not in the same group";
"Ornithopter can go after Hinata, but before Soulfire Eruption ... It can't get haste, so it only
matters as a target for Soulfire and Crackle"; "Land should be placed at the start"; "Hinata should
be higher in the list. It should be right after Izzet Signet, since a drawn Hinata can still be
played later"; Reality Spasm "once mana rocks, land and Hinata are out it is fair game"; "we need to
avoid condemning until our land is played. Because Reality Spasm scales based on the number of
lands"; then "a new rock can also add to its total ... uncondemn on land drop or rock played"; and
finally "I don't want any general exemptions" / "Those need to be deleted".

NOTHING BELOW IS PRICED YET. The order that hloo measured no longer exists -- Hinata moved 11->6,
Reality Spasm 15->7, the find tier renumbered, the rocks split 4/5, the dork 10->14, the land drop
pinned at slot 0 -- and the four type exemptions are gone. So this run is the first read on ALL of
it, and the previous Hinata numbers (full order +0.0213/+0.0193) do not carry over.

WHY THE EXEMPTION DELETION IS MEASURED ON KITTY AND MIRRORWING TOO, not just Hinata. Those four
exemptions were each root-caused from a REAL regression on those decks -- the mana/ritual one from
Mirrorwing's Gold Rush (train gi=13, a T4 win becoming T5), the peer half from KittyEquipment
gi=1325 (two Equipment at one rank, the drawn Plains affording the second draw). Deleting them may
resurface exactly those games. Per the USER's ruling the fix is then an ORDER change, not a restored
exemption, so this run's job is to say WHICH games break, not to justify putting a clause back.

THE OPEN QUESTION THIS IS DESIGNED TO ANSWER. With the type exemptions gone, the cantrip sites
(rank 10) finally have a non-empty condemnable set for the first time: Sol Ring 4, Izzet Signet 5,
Hinata 6, Reality Spasm 7 and Gamble 8 all sit strictly earlier. Whether that converts into a real
work saving is the thing condemnation has never yet delivered on this deck -- greedy-free it was
1,703 drops per 1,224,864 consultations (0.14%) for 0.9995 units. Read the units column first.

NOT IMPLEMENTED, DELIBERATELY (USER: "let's see what the games actually say before implementing
something like this"): Irencrag before Magma Opus conditional on a low enough opponent life total.
The arms below do not encode it; if the games show the Irencrag/Opus order costing, that is the
evidence for building it, and MTG_HINATA_IREN_EARLY is here to price the position unconditionally.
"""
import json
import sys

H  = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")
KT = ("decks/KittyEquipment/KittyEquipment.cod", "decks/KittyEquipment/KittyEquipment.profile.json")
MW = ("decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
      "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json")

ORD  = {"MTG_HINATA_ORDER_FULL": True}
COND = {"MTG_BP_CLASSIFY": True, "MTG_BP_SITE3": True}
NG   = {"MTG_BP_NO_GREEDY_CONT": True}

DECKS = {
    "hinata": (H, {
        "base":      {},                                   # shipped generic tiering
        "ord":       {**ORD},                              # the REVISED order alone
        "ord_iren":  {**ORD, "MTG_HINATA_IREN_EARLY": True},   # price the Irencrag position again
        "cond":      {**ORD, **COND},                      # + condemnation, no type exemptions
        "cond_ng":   {**ORD, **COND, **NG},                # + greedy-free: the DECISION-SPACE arm
        # The land drop as slot 0 actually enforced (LandDropCastOrderRank is declared but inert
        # until this flag): "Land should be placed at the start."
        "cond_ng_land": {**ORD, **COND, **NG, "MTG_BP_CONDEMN_LAND": True},
        # Prices the new mana-growth guard itself by turning it OFF. Expect WORSE: without it a
        # decline made before the land drop or a Sol Ring is treated as settled when it is not.
        "cond_ng_nosettle": {**ORD, **COND, **NG, "MTG_BP_CONDEMN_LAND_SETTLED": False},
    }),
    # The two decks whose regressions PRODUCED the deleted exemptions. Straight regression check.
    "kitty": (KT, {
        "base": {},
        "cond": {"MTG_KE_CONDEMN": True, "MTG_KE_ORDER_FULL": True},
    }),
    "mirrorwing": (MW, {
        "base": {},
        "cond": {"MTG_BP_CLASSIFY": True, "MTG_BP_CONDEMN_ORDER_AWARE": True},
    }),
}

# Disjoint blocks, spaced far wider than games-per-job (base seeds spaced under the job size REPLAY
# games; the tell is zero variance).
BLOCKS = {"train": 5500001, "hold": 6600001}
GAMES = 3000


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for deck, ((path, prof), arms) in DECKS.items():
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
