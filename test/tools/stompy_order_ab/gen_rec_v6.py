#!/usr/bin/env python3
"""Recovery manifest for every slower game of the v6 arms: <v6_dir>/slower.<arm>.txt (deck cell gi old new)
under that arm's flags, stage 1 (--depth max(shipped, old) --budget-ms 100) and stage 2 (--depth 8
--budget-ms 0). usage: gen_rec_v6.py <v6_dir> <out.json> [arm ...]   (default arms: ofixall fixall onotopall)
Job names <deck>.<cell>.<arm>-s<stage>.g<gi> (recov_v6.py reads them)."""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
FIX = {"MTG_HOLD_RELEASE_YIELD": True}
OF = {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True}
ENG = {"MTG_SAC_VICTIM_SAME_NAME": True, "MTG_EXEC_PUT_DEFERRED": True, "MTG_HOLD_PRODUCER_FIRST": True}
FLAGS = {"ofixall": dict(OF, **ENG), "onotopall": dict(OF, MTG_STOMPY_TOP_RESOLVE=False, **ENG),
         "fixall": dict(FIX, **ENG), "ofixsvnpd": dict(OF, MTG_SAC_VICTIM_SAME_NAME=True, MTG_EXEC_PUT_DEFERRED=True),
         "ofix": dict(OF), "onotop": dict(OF, MTG_STOMPY_TOP_RESOLVE=False)}
cases = open(os.path.join(WT, "test/regression_cases.sh")).read()


def deck_map(name):
    m = re.search(r"declare -A %s=\((.*?)\n\)" % name, cases, re.S)
    o = {}
    for line in m.group(1).splitlines():
        mm = re.match(r'\s*\[(\w+)\]=("?)(.*?)\2\s*$', line)
        if mm:
            o[mm.group(1)] = mm.group(3)
    return o


FILES = deck_map("DECK_FILE")
PROFS = deck_map("DECK_PROF")


def shipped_depth(deck_file):
    vp = os.path.splitext(os.path.join(WT, deck_file))[0] + ".value.json"
    try:
        return int(json.load(open(vp)).get("value_play", {}).get("target_depth", 5))
    except Exception:
        return 5


d6, out = sys.argv[1], sys.argv[2]
arms = sys.argv[3:] or ["ofixall", "fixall", "onotopall"]
jobs = []
for arm in arms:
    p = os.path.join(d6, "slower.%s.txt" % arm)
    if not os.path.exists(p):
        continue
    for row in open(p):
        f = row.split()
        if not f:
            continue
        deck, cell, gi, wo = f[0], f[1], int(f[2]), int(f[3])
        b = deck[:-3] if deck.endswith("2hg") else deck
        for stage in (1, 2):
            j = {"name": "%s.%s.%s-s%d.g%d" % (deck, cell, arm, stage, gi),
                 "deck": os.path.join(WT, FILES[b]), "profile": os.path.join(WT, PROFS[b]),
                 "games": 1, "seed": S0 + gi, "game_index": gi, "ignore_play_profile": True,
                 "depth": max(shipped_depth(FILES[b]), wo) if stage == 1 else 8,
                 "budget_ms": 100 if stage == 1 else 0, "flags": FLAGS[arm]}
            if deck.endswith("2hg"):
                j["starting_life"] = 30
                j["opponent_heads"] = 2
            jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
