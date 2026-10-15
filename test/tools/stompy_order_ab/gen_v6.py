#!/usr/bin/env python3
"""v6 A/B (2026-10-15): the Natural Order target-rank lever (MTG_STOMPY_NO_TARGET_RANK) on top of the order
and the payment fix. ONE pooled batch on one binary, every arm by per-job flags, every StompySurprise cell
(20-life and 2HG), twice the v4 sizes:
  off       -- defaults (no order, no fix): the committed tree's play
  ofix      -- the order (+ searched slots + TOP_RESOLVE opt-in) + the payment fix (the v4 arm)
  onotop    -- ofix without the TOP_RESOLVE opt-in
  ofixntr   -- ofix + the target-rank lever (the candidate)
  onotopntr -- onotop + the target-rank lever
off / ofix / onotop on games 0..(v4 size) also re-check the v4 arms game for game (the lever is default
OFF, so the binary is play-identical there).
usage: gen_v6.py <out_dir>
"""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
CHUNK = 250
out_dir = sys.argv[1]
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
CELLS = {
    "ship": {},
    "d3":   {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
    "d0":   {"depth": 0, "budget_ms": 0, "ignore_play_profile": True},
}
OF = {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True}
NOTOP = {"MTG_STOMPY_TOP_RESOLVE": False}
NTR = {"MTG_STOMPY_NO_TARGET_RANK": True}
FLAGS = {
    "off": None,
    "ofix": dict(OF),
    "onotop": dict(OF, **NOTOP),
    "ofixntr": dict(OF, **NTR),
    "onotopntr": dict(dict(OF, **NOTOP), **NTR),
}


def add(jobs, deck_key, cell, lo, hi, tag):
    base = deck_key[:-3] if deck_key.endswith("2hg") else deck_key
    off = lo
    while off < hi:
        n = min(CHUNK, hi - off)
        j = {"name": "%s.%s.%s.c%d" % (deck_key, cell, tag, off),
             "deck": os.path.join(WT, FILES[base]), "profile": os.path.join(WT, PROFS[base]),
             "games": n, "seed": S0 + off, "game_index": off}
        if deck_key.endswith("2hg"):
            j["starting_life"] = 30
            j["opponent_heads"] = 2
        j.update(CELLS[cell])
        if FLAGS[tag]:
            j["flags"] = FLAGS[tag]
        jobs.append(j)
        off += n


SIZES = {"stompy": {"ship": 8000, "d3": 12000, "d0": 40000},
         "stompy2hg": {"ship": 4000, "d3": 6000, "d0": 20000}}
jobs = []
for dk, cells in SIZES.items():
    for cell, n in cells.items():
        for tag in FLAGS:
            add(jobs, dk, cell, 0, n, tag)
os.makedirs(os.path.join(out_dir, "wins"), exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(out_dir, "manifest.json"), "w"), indent=1)
print("%d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))
