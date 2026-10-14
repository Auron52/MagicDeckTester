#!/usr/bin/env python3
"""Manifest generator for the StompySurprise order A/B (2026-10-14).

One manifest per binary (the base binary cannot read the new levers, so the two arms are two
processes run CONCURRENTLY on the same cores -- no barrier between them). Every job is a chunk of a
cell: seed = S0 + off, game_index = off, so game gi of a cell is `--seed S0+gi --game-index gi`.

usage: gen_manifest.py <out_dir> <arm: base|new> [--scale F]
"""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))   # the worktree root
S0 = 10900001
CHUNK = 250

out_dir = sys.argv[1]
arm = sys.argv[2]
scale = 1.0
if "--scale" in sys.argv:
    scale = float(sys.argv[sys.argv.index("--scale") + 1])

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
    # name: (fields)
    "ship": {},                                                        # value_play owns depth + budget
    "d3":   {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
    "d0":   {"depth": 0, "budget_ms": 0, "ignore_play_profile": True},
}

jobs = []


def add(deck_key, cell, games, tag, flags=None, two_hg=False, chunk=CHUNK):
    base = deck_key[:-3] if deck_key.endswith("2hg") else deck_key
    games = int(round(games * scale))
    off = 0
    while off < games:
        n = min(chunk, games - off)
        j = {
            "name": "%s.%s.%s.c%d" % (deck_key, cell, tag, off),
            "deck": os.path.join(WT, FILES[base]),
            "profile": os.path.join(WT, PROFS[base]),
            "games": n,
            "seed": S0 + off,
            "game_index": off,
        }
        if two_hg:
            j["starting_life"] = 30
            j["opponent_heads"] = 2
        j.update(CELLS[cell])
        if flags:
            j["flags"] = flags
        jobs.append(j)
        off += n


SIZES = {"stompy": {"ship": 4000, "d3": 6000, "d0": 20000},
         "stompy2hg": {"ship": 2000, "d3": 3000, "d0": 10000}}

# The A/B proper: the arm's binary at its defaults.
for dk, cells in SIZES.items():
    for cell, n in cells.items():
        add(dk, cell, n, arm, two_hg=dk.endswith("2hg"))

# The off switch: the NEW binary with MTG_STOMPY_ORDER=0 must be byte-identical to the base binary.
# Run on a prefix of the same seeds, NEW process only (diffed against the base arm's same games).
OFF = {"stompy": {"ship": 400, "d3": 600, "d0": 2000}, "stompy2hg": {"ship": 200, "d3": 300, "d0": 1000}}
for dk, cells in (OFF.items() if arm == "new" else []):
    for cell, n in cells.items():
        add(dk, cell, n, "off", flags={"MTG_STOMPY_ORDER": False}, two_hg=dk.endswith("2hg"))

# Identity: every other suite deck, every cell, 24 games, at defaults.
for dk in sorted(FILES):
    if dk == "stompy":
        continue
    for cell in CELLS:
        add(dk, cell, 24, "id", chunk=24)

os.makedirs(out_dir, exist_ok=True)
path = os.path.join(out_dir, "manifest.%s.json" % arm)
with open(path, "w") as f:
    json.dump({"jobs": jobs}, f, indent=1)
tot = sum(j["games"] for j in jobs)
print("%s: %d jobs, %d games" % (path, len(jobs), tot))
