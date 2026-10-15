#!/usr/bin/env python3
"""Manifests for the v3 resume of the StompySurprise order A/B (2026-10-15, box-wide lock era).

ONE pooled batch on the NEW binary holds every StompySurprise arm, because the old arm is the new binary
with MTG_STOMPY_ORDER=0 -- the off switch, which the base binary reproduces exactly (smoke GT, and the
2,750 shipped games of ab_v2 are re-run here under the off switch as the proof). Arms (job tag):
  new    -- the binary's defaults (the USER order + searched dual slots + the TOP_RESOLVE opt-in)
  off    -- MTG_STOMPY_ORDER=0 (today's behaviour: the old arm)
  notop  -- MTG_STOMPY_TOP_RESOLVE=0 (the order ALONE, without the reset: what TOP_RESOLVE buys)
  id     -- every other suite deck at defaults (identity vs the base binary)
A small manifest for the BASE binary holds the identity jobs and a per-cell prefix of the old arm, run in
the same lock acquisition, concurrently.

usage: gen_manifest_v3.py <out_dir>
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
FLAGS = {"new": None, "off": {"MTG_STOMPY_ORDER": False}, "notop": {"MTG_STOMPY_TOP_RESOLVE": False},
         "base": None, "id": None}


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
        # ab_v2 already holds the new arm's shipped stompy games 0..2749.
        lo = 2750 if (dk, cell) == ("stompy", "ship") else 0
        add(new_jobs, dk, cell, lo, n, "new")
        add(new_jobs, dk, cell, 0, n, "off")
        add(new_jobs, dk, cell, 0, n, "notop")
# The base binary: a per-cell prefix of the old arm (the off switch must equal it game for game).
PREFIX = {"stompy": {"d3": 600, "d0": 2000}, "stompy2hg": {"ship": 200, "d3": 300, "d0": 1000}}
for dk, cells in PREFIX.items():
    for cell, n in cells.items():
        add(base_jobs, dk, cell, 0, n, "base")
# Identity: every other suite deck, every cell, 24 games, both binaries at defaults.
for dk in sorted(FILES):
    if dk == "stompy":
        continue
    for cell in CELLS:
        add(new_jobs, dk, cell, 0, 24, "id", chunk=24)
        add(base_jobs, dk, cell, 0, 24, "id", chunk=24)

os.makedirs(out_dir, exist_ok=True)
for nm, jobs in (("new", new_jobs), ("base", base_jobs)):
    p = os.path.join(out_dir, "manifest.%s.json" % nm)
    json.dump({"jobs": jobs}, open(p, "w"), indent=1)
    print("%s: %d jobs, %d games" % (p, len(jobs), sum(j["games"] for j in jobs)))
