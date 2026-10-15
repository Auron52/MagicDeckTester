#!/usr/bin/env python3
"""Battlefield-order sensitivity of the gi1526 T3 kill: write one scenario per permutation of the
non-land permanents (Forests first). usage: bforder.py <out_dir>"""
import itertools
import json
import os
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
base = json.load(open(os.path.join(WT, "test/tools/stompy_order_ab/scn_1526_t3.json")))
base.pop("expect_win_turn", None)
perms = ["Sol Ring", "Fyndhorn Elves", "Priest of Titania", "Elvish Mystic"]
for k, p in enumerate(itertools.permutations(perms)):
    j = dict(base)
    bf = [{"name": "Forest", "controller": 0, "tapped": False}, {"name": "Forest", "controller": 0, "tapped": False}]
    for n in p:
        e = {"name": n, "controller": 0, "tapped": False}
        if n != "Sol Ring":
            e["sick"] = False
        bf.append(e)
    j["battlefield"] = bf
    j["depth"] = 2
    j["_order"] = list(p)
    json.dump(j, open(os.path.join(out, "p%02d.json" % k), "w"), indent=1)
print("ok")
