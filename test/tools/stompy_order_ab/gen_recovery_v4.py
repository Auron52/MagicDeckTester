#!/usr/bin/env python3
"""Recovery jobs (stage 1 and stage 2 together, one game each) for the slower games of a v4 arm.

usage: gen_recovery_v4.py <slower.<arm>.txt> <arm> <out.json>
  stage 1: --depth max(shipped depth, old win turn) --budget-ms 100; stage 2: --depth 8 --budget-ms 0.
The arm's flags ride every job (the game must recover under the arm being adopted). The shipped depth is
read from the deck's value_play block (target_depth) when present, else 5.
"""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
FLAGS = {"fix": {"MTG_HOLD_RELEASE_YIELD": True},
         "ofix": {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True},
         "onotop": {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True, "MTG_STOMPY_TOP_RESOLVE": False},
         "order": {"MTG_STOMPY_ORDER": True}}
cases = open(os.path.join(WT, "test/regression_cases.sh")).read()


def deck_map(name):
    m = re.search(r"declare -A %s=\((.*?)\n\)" % name, cases, re.S)
    out = {}
    for line in m.group(1).splitlines():
        mm = re.match(r'\s*\[(\w+)\]=("?)(.*?)\2\s*$', line)
        if mm:
            out[mm.group(1)] = mm.group(3)
    return out


FILES = deck_map("DECK_FILE")
PROFS = deck_map("DECK_PROF")


def shipped_depth(deck_file):
    stem = os.path.splitext(os.path.join(WT, deck_file))[0]
    vp = stem + ".value.json"
    try:
        j = json.load(open(vp))
        return int(j.get("value_play", {}).get("target_depth", 5))
    except Exception:
        return 5


slower, arm, out = sys.argv[1], sys.argv[2], sys.argv[3]
jobs = []
for row in open(slower):
    f = row.split()
    if not f:
        continue
    deck, cell, gi, wo = f[0], f[1], int(f[2]), int(f[3])
    base = deck[:-3] if deck.endswith("2hg") else deck
    for stage in (1, 2):
        j = {"name": "%s.%s.s%d.g%d" % (deck, cell, stage, gi), "deck": os.path.join(WT, FILES[base]),
             "profile": os.path.join(WT, PROFS[base]), "games": 1, "seed": S0 + gi, "game_index": gi,
             "ignore_play_profile": True, "flags": FLAGS[arm]}
        if stage == 1:
            j["depth"] = max(shipped_depth(FILES[base]), wo)
            j["budget_ms"] = 100
        else:
            j["depth"] = 8
            j["budget_ms"] = 0
        if deck.endswith("2hg"):
            j["starting_life"] = 30
            j["opponent_heads"] = 2
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print("%d jobs -> %s" % (len(jobs), out))
