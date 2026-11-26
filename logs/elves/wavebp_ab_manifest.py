#!/usr/bin/env python3
"""Held-out A/B manifest for the site-10 battlefield-put fix: arms INNERMOST, order rotated per chunk.

    wavebp_ab_manifest.py <smoke manifest.json> <key:games> ... > ab.json

Each <key> is a smoke job-name stem (e.g. landfall, pd2hg); its job template (deck, profile, budget_ms, 2HG fields)
is copied from the smoke tier's `<key>_smoke_d5_s1001` job, so every arm plays the suite's d5 row shape (depth
omitted -> the profile's play depth, as the harness does). The key `elves` (not in the suite) uses d5 b20.

base = MTG_BP_PUT_BATTLEFIELD off (the hatch; smoke under it is 243/243 PASS against the committed GT, i.e. it IS the
pre-fix play), new = default. Each chunk is 10 games (seed BASE+c, game_index c) played by both arms, so every game
is paired. Job names are "<arm>__<key>_c<c>" (scripts/ab_wall.py's pairing shape). same_seed_inflight 0 (default):
a seed's arms run side by side, so wall is comparable (regression-testing.md, "Measuring WALL in a pooled A/B").
"""
import json
import sys

BASE = 9600001
CHUNK = 10


def main():
    smoke = {j["name"]: j for j in json.load(open(sys.argv[1]))["jobs"]}
    specs = []
    for a in sys.argv[2:]:
        key, _, games = a.partition(":")
        if key == "elves":
            tpl = {"deck": "decks/Elves/Elves.cod", "profile": "decks/Elves/Elves.profile.json", "budget_ms": 20}
        else:
            src = smoke["%s_smoke_d5_s1001" % key]
            tpl = {k: v for k, v in src.items() if k not in ("name", "games", "seed", "weight")}
        specs.append((key, tpl, int(games)))
    arms = [("base", {"MTG_BP_PUT_BATTLEFIELD": False}), ("new", None)]
    jobs = []
    ci = 0
    maxg = max(g for _, _, g in specs)
    for c in range(0, maxg, CHUNK):          # decks interleaved chunk by chunk: one tail for the whole pool
        for key, tpl, games in specs:
            if c >= games:
                continue
            order = arms if ci % 2 == 0 else arms[::-1]
            ci += 1
            for arm, flags in order:
                j = {"name": "%s__%s_c%d" % (arm, key, c)}
                j.update(tpl)
                j.update({"games": CHUNK, "seed": BASE + c, "game_index": c})
                if flags:
                    j["flags"] = flags
                jobs.append(j)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)


if __name__ == "__main__":
    main()
