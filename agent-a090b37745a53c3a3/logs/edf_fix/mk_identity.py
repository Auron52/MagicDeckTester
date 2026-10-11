#!/usr/bin/env python3
"""Identity manifests: the same jobs with my five levers forced OFF (flags) or left at their defaults.

usage: mk_identity.py <out.json> <mode: off|default> [d0_games] [d3_games]
Decks: EDF, Fungus, Bruna (the land-Aura decks), burn, Dragonstorm, Hinata2 (no land Aura, no route).
Seeds 7100001 (d0) / 7110001 (d3): unrelated to ab3 and to the held-out run.
"""
import json, sys

ROOT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3"
DECKS = {
    "edf":         ("decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
                    "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json"),
    "fungus":      ("decks/Fungus/Fungus.cod", "decks/Fungus/Fungus.profile.json"),
    "bruna":       ("decks/Bruna/Bruna.cod", "decks/Bruna/Bruna.profile.json"),
    "burn":        ("decks/burn/burn.txt", "decks/burn/burn.profile.json"),
    "dragonstorm": ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
    "hinata":      ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
}
LEVERS = ["MTG_SOLVE_UNTAP_CREDIT", "MTG_PREPAY_ENERGY_RESTORE", "MTG_SOLVE_AURA_PAYER_CHECK",
          "MTG_EDF_ROUTE_BOARD_DEPLOY", "MTG_EDF_GOFF_VERIFY_UNPROJECTED"]

def main():
    out, mode = sys.argv[1], sys.argv[2]
    n0 = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    n3 = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    jobs = []
    for key, (deck, prof) in DECKS.items():
        for cell, n, seed, extra in (("d3", n3, 7110001, {"depth": 3, "budget_ms": 10}),
                                     ("d0", n0, 7100001, {"depth": 0})):
            j = {"name": "%s_%s" % (key, cell), "deck": ROOT + "/" + deck, "profile": ROOT + "/" + prof,
                 "games": n, "seed": seed, "ignore_play_profile": True}
            j.update(extra)
            if mode == "off":
                j["flags"] = {l: False for l in LEVERS}
            jobs.append(j)
    json.dump({"jobs": jobs}, open(out, "w"), indent=1)
    print("wrote %s: %d jobs" % (out, len(jobs)))

if __name__ == "__main__":
    main()
