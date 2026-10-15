#!/usr/bin/env python3
"""Manifests for the v4 A/B (2026-10-15): the StompySurprise order with the payment fix.

The NEW binary (order default OFF, fix default OFF) runs every arm in ONE pooled batch via per-job flags:
  off    -- defaults: no order, no fix (the old arm; must equal the base binary)
  fix    -- MTG_HOLD_RELEASE_YIELD=1 alone (the payment fix; it can move any deck with mana creatures)
  ofix   -- the order (+ its searched slots + the TOP_RESOLVE opt-in, as built) + the fix
  onotop -- the order + the fix, WITHOUT the TOP_RESOLVE opt-in (what the reset buys)
  order  -- the order without the fix (the 10-14 arm, for attribution)
StompySurprise gets the full sizes; every other suite deck gets the fix screen (off vs fix) at SCREEN
games per cell. The BASE binary runs every suite deck at defaults (SCREEN games per cell) and a prefix of
StompySurprise, so the new binary's off arm is checked game for game.

usage: gen_manifest_v4.py <out_dir> [--screen N]
"""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
CHUNK = 250
out_dir = sys.argv[1]
SCREEN = int(sys.argv[sys.argv.index("--screen") + 1]) if "--screen" in sys.argv else 100
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
ON = True
FLAGS = {
    "off": None,
    "fix": {"MTG_HOLD_RELEASE_YIELD": ON},
    "ofix": {"MTG_HOLD_RELEASE_YIELD": ON, "MTG_STOMPY_ORDER": ON},
    "onotop": {"MTG_HOLD_RELEASE_YIELD": ON, "MTG_STOMPY_ORDER": ON, "MTG_STOMPY_TOP_RESOLVE": False},
    "order": {"MTG_STOMPY_ORDER": ON},
    "base": None,
}


def add(jobs, deck_key, cell, lo, hi, tag, chunk=CHUNK):
    base = deck_key[:-3] if deck_key.endswith("2hg") else deck_key
    off = lo
    while off < hi:
        n = min(chunk, hi - off)
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


SIZES = {"stompy": {"ship": 4000, "d3": 6000, "d0": 20000},
         "stompy2hg": {"ship": 2000, "d3": 3000, "d0": 10000}}
new_jobs, base_jobs = [], []
for dk, cells in SIZES.items():
    for cell, n in cells.items():
        for tag in ("off", "fix", "ofix", "onotop", "order"):
            add(new_jobs, dk, cell, 0, n, tag)
# The base binary: a StompySurprise prefix per cell (the off arm must equal it game for game).
PREFIX = {"stompy": {"ship": 400, "d3": 600, "d0": 2000}, "stompy2hg": {"ship": 200, "d3": 300, "d0": 1000}}
for dk, cells in PREFIX.items():
    for cell, n in cells.items():
        add(base_jobs, dk, cell, 0, n, "base")
# Every other suite deck: the fix screen (new binary, off + fix) and the base binary at defaults.
for dk in sorted(FILES):
    if dk == "stompy":
        continue
    for cell in CELLS:
        add(new_jobs, dk, cell, 0, SCREEN, "off", chunk=SCREEN)
        add(new_jobs, dk, cell, 0, SCREEN, "fix", chunk=SCREEN)
        add(base_jobs, dk, cell, 0, SCREEN, "base", chunk=SCREEN)

os.makedirs(out_dir, exist_ok=True)
for nm, jobs in (("new", new_jobs), ("base", base_jobs)):
    p = os.path.join(out_dir, "manifest.%s.json" % nm)
    json.dump({"jobs": jobs}, open(p, "w"), indent=1)
    print("%s: %d jobs, %d games" % (p, len(jobs), sum(j["games"] for j in jobs)))
