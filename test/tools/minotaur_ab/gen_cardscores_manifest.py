#!/usr/bin/env python3
"""Manifest for the Minotaur LEARNED-VALUE discard sweep -- ONE pooled batch, every arm and depth.

The question: the adopted EV model is `P(play) x value`, and `value` is an AUTHORED order. The
deck's profile carries LEARNED `card_scores`. Is the learned order the better value term?

Three arms, chosen to DISCRIMINATE rather than merely to differ (a mechanism that fits is not one
that separates hypotheses):
  csval  value order taken from card_scores, P kept.  -- is the learned ORDER better?
  csnop  the same order with P DROPPED.               -- are card_scores already an EV?
  csdup  adopted order, but the learned per-copy marginal decides whether the duplicate penalty
         applies at all.                              -- is the learned MARGINAL usable even if
                                                        the learned LEVEL is not?
csval vs csnop is the discriminating pair. card_scores are an OPENING-HAND marginal, so castability
is already inside them; if that is the dominant confound, multiplying by P double-counts the mana
and csnop should beat csval. If instead the learned order is simply better information, csval wins.
Either arm alone would produce a number that cannot tell those apart.

Seeds are disjoint from every prior Minotaur sweep (940k-1.09M, 1.1M-1.41M, 1.5M-1.81M, 2.0M-2.31M,
3.0M-3.99M, 4.0M-4.63M), so this is a clean SELECTION block; hold 6.0M+ back for confirmation.

Usage:  gen_cardscores_manifest.py > logs/minotaur_cs/manifest.json
"""
import json

DECK    = "decks/Minotaur/Minotaur.cod"
PROFILE = "decks/Minotaur/Minotaur.profile.json"
GAMES   = 5000
SEED0   = 5_000_000
NSEEDS  = 32
STEP    = 10_000

ARMS = {
    "base":  {},                                          # the adopted model, as shipped
    "csval": {"MTG_MINOTAUR_DISCARD_CSVAL": True},
    "csnop": {"MTG_MINOTAUR_DISCARD_CSNOP": True},
    "csdup": {"MTG_MINOTAUR_DISCARD_CSDUP": True},
}

jobs = []
for depth in (0, 3):
    for k in range(NSEEDS):
        seed = SEED0 + k * STEP
        for arm, flags in ARMS.items():
            job = {
                "name": f"{arm}_d{depth}_s{seed}",
                "deck": DECK,
                "profile": PROFILE,
                "games": GAMES,
                "seed": seed,
                "depth": depth,
                "budget_ms": 0,
                # The deck's value_play sidecar LOCKS the play depth; this sweep varies depth on
                # purpose, so the lock is bypassed. The profile itself still attaches -- which
                # matters here more than usual, since card_scores come off it.
                "ignore_play_profile": True,
                "weight": 0,
            }
            if flags:
                job["flags"] = flags
            jobs.append(job)

print(json.dumps({"jobs": jobs}, indent=1))
