#!/usr/bin/env python3
"""Byte-identity sample: Dragonstorm d0/d3/ship + Hinata2 + Mirrorwing d0/d3, one pooled batch.

    python3 make_ident.py <outdir>
Run it on the BASE binary (no flags) and on the new binary with MTG_DRAGONSTORM_USER_ORDER=0 in the env;
every job digest must match.
"""
import json, os, sys

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6"
DECKS = {
    "ds": (WT + "/decks/Dragonstorm/Dragonstorm.cod", WT + "/decks/Dragonstorm/Dragonstorm.profile.json"),
    "hin": (WT + "/decks/Hinata2/Hinata2.cod", WT + "/decks/Hinata2/Hinata2.profile.json"),
    "mw": (WT + "/decks/Mirrorwing Dragon/Mirrorwing Dragon.cod",
           WT + "/decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
}
# (deck, cell, seed, games, depth, budget)
CELLS = [("ds", "d0", 7500001, 200, 0, 0), ("ds", "d3", 7510001, 60, 3, 10), ("ds", "ship", 7520001, 40, None, 20),
         ("hin", "d0", 7530001, 100, 0, 0), ("hin", "d3", 7540001, 20, 3, 10),
         ("mw", "d0", 7550001, 100, 0, 0), ("mw", "d3", 7560001, 20, 3, 10)]
CHUNK = 20


def main():
    out = sys.argv[1]
    jobs = []
    for deck, cell, seed, games, depth, bud in CELLS:
        d, p = DECKS[deck]
        for c in range(0, games, CHUNK):
            j = {"name": "%s_%s.c%d" % (deck, cell, c), "deck": d, "profile": p, "games": min(CHUNK, games - c),
                 "seed": seed + c, "game_index": c, "budget_ms": bud}
            if depth is not None:
                j["depth"] = depth
                j["ignore_play_profile"] = True
            jobs.append(j)
    jobs.sort(key=lambda j: 0 if ("ship" in j["name"] or "d3" in j["name"]) else 1)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))


main()
