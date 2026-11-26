#!/usr/bin/env python3
"""Held-out manifest for the site-10 battlefield-put cost work (logs/bpput/report.md).

    manifest.py <wavebp ab.json> <base_seed> <arm>[,<arm>...] <key:games> ... > m.json

Job templates (deck, profile, budget_ms, 2HG fields) are copied from the wavebp A/B's `new__<key>_c0` job, i.e. the
suite's d5 row shape. 10-game chunks, seed BASE+c / game_index c, arms innermost and alternated per chunk, decks
interleaved chunk by chunk (one tail). An arm here is only a job-name prefix: each binary runs the same manifest.
"""
import json
import sys

CHUNK = 10


def main():
    src = {j["name"]: j for j in json.load(open(sys.argv[1]))["jobs"]}
    base = int(sys.argv[2])
    arms = sys.argv[3].split(",")
    specs = []
    for a in sys.argv[4:]:
        key, _, games = a.partition(":")
        tpl = {k: v for k, v in src["new__%s_c0" % key].items() if k not in ("name", "games", "seed", "game_index", "flags")}
        specs.append((key, tpl, int(games)))
    jobs, ci = [], 0
    for c in range(0, max(g for _, _, g in specs), CHUNK):
        for key, tpl, games in specs:
            if c >= games:
                continue
            order = arms if ci % 2 == 0 else arms[::-1]
            ci += 1
            for arm in order:
                j = {"name": "%s__%s_c%d" % (arm, key, c)}
                j.update(tpl)
                j.update({"games": CHUNK, "seed": base + c, "game_index": c})
                jobs.append(j)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)


if __name__ == "__main__":
    main()
