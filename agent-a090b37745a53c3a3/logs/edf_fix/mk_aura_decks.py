#!/usr/bin/env python3
"""Paired E2b (MTG_SOLVE_AURA_PAYER_CHECK) ON vs OFF on the other two land-Aura decks (Wild Growth):
Bruna and Fungus. No other new lever can reach them (no ETB-untap creature, no energy, no combo route).
Job names follow analyze.py's edf_<cell>_<arm> pattern with the deck folded into the cell name.
Seeds 5330001 (d0) / 5340001 (d3 b10), outside ab3 and the EDF held-out."""
import json
WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3"
S = "/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3"
DECKS = {"bruna": ("decks/Bruna/Bruna.cod", "decks/Bruna/Bruna.profile.json"),
         "fungus": ("decks/Fungus/Fungus.cod", "decks/Fungus/Fungus.profile.json")}
jobs = []
for key, (deck, prof) in DECKS.items():
    for cell, n, seed, extra in (("d3", 100, 5340001, {"depth": 3, "budget_ms": 10}),
                                 ("d0", 2000, 5330001, {"depth": 0})):
        for arm in ("on", "L2b"):
            j = {"name": "edf_%s%s_%s" % (key, cell, arm), "deck": WT + "/" + deck, "profile": WT + "/" + prof,
                 "games": n, "seed": seed, "ignore_play_profile": True}
            j.update(extra)
            if arm == "L2b":
                j["flags"] = {"MTG_SOLVE_AURA_PAYER_CHECK": False}
            jobs.append(j)
json.dump({"jobs": jobs}, open(S + "/aura_decks.json", "w"), indent=1)
print("wrote %s/aura_decks.json: %d jobs" % (S, len(jobs)))
