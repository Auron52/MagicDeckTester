#!/usr/bin/env python3
"""Condemnation's two unsound exemptions, root-caused on Hinata and priced on every deck that
ships condemnation. ONE pooled batch.

ROOT CAUSE (2026-08-26). Condemnation measured NEGATIVE on Hinata in every configuration. All 75
regressions (peer -> peeroa) reproduce exactly and NONE is a mulligan divergence, so every one is a
real play difference. MTG_CONDEMN_WHO over five of the losing games: 4,541 condemnations, exactly
two victims --

    Gamble         3,869  (rank 6)   at Ponder / Preordain sites (rank 7)
    Reality Spasm    665  (rank 15)  at Soulfire Eruption sites  (rank 20)

Both are the failure the EXISTING mana exemption was built for, falling through its test:

  * RITUAL. The mana exemption's argument -- "an accelerant is cast when the rest of the turn needs
    the mana, and how much mana the turn needs is exactly what a breakpoint draw reveals" -- is
    about MANA, but it ships as `mana_rock || ManaDork`, which only recognises accelerants that are
    PERMANENTS. Reality Spasm (untap_x_mana_sources) and Irencrag Feat (ritual_floating_mana) are
    accelerants that are SPELLS. Across the 75 regressions Reality Spasm is the single most common
    cast the baseline makes and the condemnation arm does not (33), Irencrag 6 more.
  * TUTOR. The sibling-line argument needs the earlier line to cast the card at its proper position
    AND GET THE SAME THING. A tutor's fetch is chosen at resolution from the state
    (HinataProvider::TutorCandidates is combo-aware), so a tutor declined before a breakpoint was
    declined under strictly less information -- the same reason a card the breakpoint DREW is never
    condemned.

The ORDER-SIDE fix for Gamble was built (MTG_HINATA_GAMBLE_LATE, tutor after the cantrips) and
REJECTED by its own counter: it merely relocates the bug, because Gamble is itself a breakpoint
site, so at rank 8 it condemns the rank-7 cantrips instead (Preordain 2,400 + Ponder 1,140). It is
not measured here. Counters over the same five games:

    current 4,541 | +ritual 3,874 | +tutor 207 | +gamble_late 4,271 | +ritual+tutor 6

THE EXEMPTIONS ARE ENGINE-WIDE, so they are priced on the decks that actually ship condemnation via
CondemnsConsideredAtBreakpoint -- Anti-Lifegain and KittyEquipment -- not only on the deck that
root-caused them. A fix that repairs Hinata and regresses Kitty is not a fix.

Mode 2 (play settings): depth/budget_ms omitted so BatchRunner takes each deck's value_play block.
Blocks match the two earlier Hinata rounds so all three are directly comparable.
"""
import json
import sys

HINATA = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")
AL     = ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json")
KITTY  = ("decks/KittyEquipment/KittyEquipment.cod", "decks/KittyEquipment/KittyEquipment.profile.json")
GAMES = 3000

FULL = {"MTG_HINATA_ORDER_FULL": True}
COND = {"MTG_BP_CLASSIFY": True, "MTG_BP_CONDEMN_ORDER_AWARE": True}
R    = {"MTG_BP_CONDEMN_RITUAL_EXEMPT": True}
T    = {"MTG_BP_CONDEMN_TUTOR_EXEMPT": True}
IREN = {"MTG_HINATA_IREN_EARLY": True}

ARMS = {
    # ---- Hinata: does fixing the two exemptions make condemnation stop losing? ----------------
    "hin_base":      (HINATA, {}),
    "hin_peer":      (HINATA, FULL),                            # order only (no condemnation)
    "hin_oa":        (HINATA, {**FULL, **COND}),                # the regressing arm
    "hin_oa_r":      (HINATA, {**FULL, **COND, **R}),           # + ritual exemption
    "hin_oa_t":      (HINATA, {**FULL, **COND, **T}),           # + tutor exemption
    "hin_oa_rt":     (HINATA, {**FULL, **COND, **R, **T}),      # + both
    "hin_iren":      (HINATA, {**FULL, **IREN}),                # best order-only arm
    "hin_iren_rt":   (HINATA, {**FULL, **IREN, **COND, **R, **T}),
    "hin_iren_rtng": (HINATA, {**FULL, **IREN, **COND, **R, **T,
                               "MTG_BP_NO_GREEDY_CONT": True}),
    # ---- the decks that already SHIP condemnation: do the exemptions cost them anything? ------
    "al_base":       (AL,    {}),
    "al_rt":         (AL,    {**R, **T}),
    "kitty_base":    (KITTY, {}),
    "kitty_rt":      (KITTY, {**R, **T}),
}

BLOCKS = {"train": 500001, "hold": 1050001}


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    jobs = []
    for arm, ((deck, prof), flags) in ARMS.items():
        for block, seed in BLOCKS.items():
            job = {
                "name":    f"{arm}.{block}",
                "deck":    deck,
                "profile": prof,
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
