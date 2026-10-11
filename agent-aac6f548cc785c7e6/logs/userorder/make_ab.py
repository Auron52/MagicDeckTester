#!/usr/bin/env python3
"""Sanity A/B (NOT a verdict): MTG_DRAGONSTORM_USER_ORDER on vs off, paired seeds, ONE pooled batch.

    python3 make_ab.py <outdir>                 -> <outdir>/manifest.json
    MTG_DUMP_WINS=1 mtg --batch <outdir>/manifest.json --threads 4 2> batch.err > batch.out
    python3 make_ab.py --analyse <outdir>/batch.err
"""
import json, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6"
DECK = WT + "/decks/Dragonstorm/Dragonstorm.cod"
PROF = WT + "/decks/Dragonstorm/Dragonstorm.profile.json"
CELLS = [("d0", 7600001, 300, 0, 0), ("d3", 7700001, 100, 3, 10)]
CHUNK = 25
MAX_TURNS = 8


def build(out):
    jobs = []
    for cname, seed, games, depth, bud in CELLS:
        for c in range(0, games, CHUNK):
            for arm, on in (("on", True), ("off", False)):
                j = {"name": "%s_%s.c%d" % (cname, arm, c), "deck": DECK, "profile": PROF,
                     "games": min(CHUNK, games - c), "seed": seed + c, "game_index": c, "budget_ms": bud,
                     "depth": depth, "ignore_play_profile": True,
                     "flags": {"MTG_DRAGONSTORM_USER_ORDER": on}}
                jobs.append(j)
    jobs.sort(key=lambda j: 0 if j["name"].startswith("d3") else 1)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))


def analyse(err):
    pat = re.compile(r"\[win\] job=(d0|d3)_(on|off)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(dict)
    for line in open(err, errors="replace"):
        m = pat.search(line)
        if not m:
            continue
        cell, arm, c, gi, wt = m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5))
        w = wt if wt > 0 else MAX_TURNS + 1
        res[(cell, gi)][arm] = w
    for cell in ("d0", "d3"):
        keys = sorted(k for k in res if k[0] == cell and len(res[k]) == 2)
        on = sum(res[k]["on"] for k in keys)
        off = sum(res[k]["off"] for k in keys)
        faster = [k for k in keys if res[k]["on"] < res[k]["off"]]
        slower = [k for k in keys if res[k]["on"] > res[k]["off"]]
        print("%s: %d paired games; turn sum on %d vs off %d (delta %+d, mean %+.4f); faster %d, slower %d"
              % (cell, len(keys), on, off, on - off, (on - off) / max(1, len(keys)), len(faster), len(slower)))
        base = dict((c[0], c[1]) for c in CELLS)[cell]
        for k in slower:
            print("   SLOWER %s gi=%d seed=%d: on %d off %d" % (cell, k[1], base + k[1], res[k]["on"], res[k]["off"]))


if __name__ == "__main__":
    if sys.argv[1] == "--analyse":
        analyse(sys.argv[2])
    else:
        build(sys.argv[1])
