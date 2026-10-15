#!/usr/bin/env python3
"""v6 A/B (2026-10-15): the StompySurprise order with the payment fix and the three engine levers the 2HG
non-recoverers produced, ONE pooled batch on one binary, every arm by per-job flags.

  MTG_SAC_VICTIM_SAME_NAME (svn) -- a stale Natural Order victim pin eats another copy of its name (gi160)
  MTG_EXEC_PUT_DEFERRED    (pd)  -- the executor defers a put-in-hand breakpoint as the rollout does (gi797)
  MTG_HOLD_PRODUCER_FIRST  (pf)  -- the prepay releases the biggest producer first, never holds a victim (gi2290)

StompySurprise, every cell (20-life and 2HG), twice the v4 sizes:
  off        -- defaults (no order, no fix): the committed tree's play
  ofix       -- the order (+ searched slots + TOP_RESOLVE opt-in) + the payment fix (the v4 arm)
  onotop     -- ofix without the TOP_RESOLVE opt-in
  ofixall    -- ofix + svn + pd + pf (the candidate)
  onotopall  -- onotop + svn + pd + pf
  ofixsvnpd  -- ofix + svn + pd (what pf adds)
  fixall     -- the order OFF: the payment fix + svn + pd + pf (the engine fixes alone)
Every OTHER suite deck, games 0..149 per cell: fixall (compared with v4's off / fix arms on the same games;
svn / pd / pf are default OFF, so the binary plays v4's arms identically).
off / ofix / onotop on StompySurprise games 0..(v4 size) re-check v4 game for game.
usage: gen_v6.py <out_dir>
"""
import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
CHUNK = 250
SCREEN = 150
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
FIX = {"MTG_HOLD_RELEASE_YIELD": True}
OF = {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True}
NOTOP = {"MTG_STOMPY_TOP_RESOLVE": False}
SVN = {"MTG_SAC_VICTIM_SAME_NAME": True}
PD = {"MTG_EXEC_PUT_DEFERRED": True}
PF = {"MTG_HOLD_PRODUCER_FIRST": True}


def merge(*ds):
    o = {}
    for d in ds:
        o.update(d)
    return o


FLAGS = {
    "off": None,
    "ofix": merge(OF),
    "onotop": merge(OF, NOTOP),
    "ofixall": merge(OF, SVN, PD, PF),
    "onotopall": merge(OF, NOTOP, SVN, PD, PF),
    "ofixsvnpd": merge(OF, SVN, PD),
    "fixall": merge(FIX, SVN, PD, PF),
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


SIZES = {"stompy": {"ship": 8000, "d3": 12000, "d0": 40000},
         "stompy2hg": {"ship": 4000, "d3": 6000, "d0": 20000}}
jobs = []
for dk, cells in SIZES.items():
    for cell, n in cells.items():
        for tag in FLAGS:
            add(jobs, dk, cell, 0, n, tag)
for dk in sorted(FILES):
    if dk == "stompy":
        continue
    for cell in CELLS:
        add(jobs, dk, cell, 0, SCREEN, "fixall", chunk=SCREEN)
os.makedirs(os.path.join(out_dir, "wins"), exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(out_dir, "manifest.json"), "w"), indent=1)
print("%d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))
