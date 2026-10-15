#!/usr/bin/env python3
"""v5 (2026-10-15): one pooled batch on the fix binary for
  (1) recovery of every slower game of the v4 arms ofix / onotop / fix -- stage 1 (--depth max(shipped,
      old) --budget-ms 100) and stage 2 (--depth 8 --budget-ms 0) pooled together, each under its arm's flags;
  (2) the fix's movers at a larger sample, fix vs off on fresh games (gi 150..): SelesnyaLifegain,
      FiveColour, Bruna.
usage: gen_v5.py <v4_dir> <out_dir>
"""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
v4, out = sys.argv[1], sys.argv[2]
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
FLAGS = {"fix": {"MTG_HOLD_RELEASE_YIELD": True},
         "ofix": {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True},
         "onotop": {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True, "MTG_STOMPY_TOP_RESOLVE": False},
         "off": None}
CELLS = {"ship": {}, "d3": {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
         "d0": {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}}


def shipped_depth(deck_file):
    vp = os.path.splitext(os.path.join(WT, deck_file))[0] + ".value.json"
    try:
        return int(json.load(open(vp)).get("value_play", {}).get("target_depth", 5))
    except Exception:
        return 5


def base_job(deck, name, seed, gi, games):
    b = deck[:-3] if deck.endswith("2hg") else deck
    j = {"name": name, "deck": os.path.join(WT, FILES[b]), "profile": os.path.join(WT, PROFS[b]),
         "games": games, "seed": seed, "game_index": gi}
    if deck.endswith("2hg"):
        j["starting_life"] = 30
        j["opponent_heads"] = 2
    return j


rec, mov = [], []
for arm in ("ofix", "onotop", "fix"):
    p = os.path.join(v4, "slower.%s.txt" % arm)
    for row in open(p):
        f = row.split()
        if not f:
            continue
        deck, cell, gi, wo = f[0], f[1], int(f[2]), int(f[3])
        b = deck[:-3] if deck.endswith("2hg") else deck
        for stage in (1, 2):
            j = base_job(deck, "%s.%s.%s-s%d.g%d" % (deck, cell, arm, stage, gi), S0 + gi, gi, 1)
            j["ignore_play_profile"] = True
            j["depth"] = max(shipped_depth(FILES[b]), wo) if stage == 1 else 8
            j["budget_ms"] = 100 if stage == 1 else 0
            if FLAGS[arm]:
                j["flags"] = FLAGS[arm]
            rec.append(j)
SIZES = {"selesnya": {"ship": 600, "d3": 1000, "d0": 3000},
         "fivecolour": {"ship": 300, "d3": 500, "d0": 2000},
         "bruna": {"ship": 150, "d3": 300, "d0": 1000}}
LO = 150   # v4 already holds games 0..149
for deck, cells in SIZES.items():
    for cell, n in cells.items():
        for tag in ("off", "fix"):
            off = LO
            while off < LO + n:
                k = min(250, LO + n - off)
                j = base_job(deck, "%s.%s.%s.c%d" % (deck, cell, tag, off), S0 + off, off, k)
                j.update(CELLS[cell])
                if FLAGS[tag]:
                    j["flags"] = FLAGS[tag]
                mov.append(j)
                off += k
os.makedirs(out, exist_ok=True)
json.dump({"jobs": rec + mov}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
print("%d recovery jobs, %d mover jobs (%d games)" % (len(rec), len(mov), sum(j["games"] for j in mov)))
