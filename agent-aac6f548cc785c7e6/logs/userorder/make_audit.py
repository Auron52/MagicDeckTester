#!/usr/bin/env python3
"""One pooled audit batch: Dragonstorm at d0, d3/b10 and shipped settings, disjoint fresh seeds.

    python3 make_audit.py <outdir> [--flags JSON] [--scale K]
"""
import json, os, sys

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6"
DECK = WT + "/decks/Dragonstorm/Dragonstorm.cod"
PROF = WT + "/decks/Dragonstorm/Dragonstorm.profile.json"
# (name, seed base, games, depth, budget)  -- seed ranges must match audit.py SETTINGS
CELLS = [("d0", 7000001, 300, 0, 0), ("d3b10", 7100001, 200, 3, 10), ("ship", 7200001, 200, None, 20)]
CHUNK = 20


def main():
    out = sys.argv[1]
    flags = {}
    if "--flags" in sys.argv:
        flags = json.loads(sys.argv[sys.argv.index("--flags") + 1])
    jobs = []
    for name, base, games, depth, bud in CELLS:
        for c in range(0, games, CHUNK):
            j = {"name": "%s.c%d" % (name, c), "deck": DECK, "profile": PROF, "games": min(CHUNK, games - c),
                 "seed": base + c, "game_index": c, "budget_ms": bud}
            if depth is not None:
                j["depth"] = depth
                j["ignore_play_profile"] = True
            if flags:
                j["flags"] = dict(flags)
            jobs.append(j)
    # ship (slowest) first so the tail is short
    jobs.sort(key=lambda j: 0 if j["name"].startswith("ship") else (1 if j["name"].startswith("d3") else 2))
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))


main()
