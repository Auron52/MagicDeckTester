#!/usr/bin/env python3
"""Which reference games' per-game TRACES differ between two ref_bench --log-root dirs (A, B), even where the
win turn did not move -- the check that a lever arm actually reached the engine.
usage: refs_tracediff.py <A_dir> <B_dir>"""
import glob
import json
import os
import sys

a_dir, b_dir = sys.argv[1], sys.argv[2]
same = diff = 0
by_deck = {}
for pa in sorted(glob.glob(os.path.join(a_dir, "*.json"))):
    pb = os.path.join(b_dir, os.path.basename(pa))
    if not os.path.exists(pb):
        continue
    ta = json.load(open(pa)).get("turns")
    tb = json.load(open(pb)).get("turns")
    deck = os.path.basename(pa).split("__")[0]
    d = by_deck.setdefault(deck, [0, 0])
    if ta == tb:
        same += 1
        d[0] += 1
    else:
        diff += 1
        d[1] += 1
for deck, (s, df) in sorted(by_deck.items()):
    if df:
        print("%-40s identical %3d  differ %3d" % (deck, s, df))
print("ALL identical %d differ %d" % (same, diff))
