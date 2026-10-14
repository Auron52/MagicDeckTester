#!/usr/bin/env python3
"""Append the jobs of other manifests to a target manifest (names must not collide), then sort the whole
queue most-expensive-first (rough per-game cost from test/suite_cost.json x cell factor).
  merge_plain.py <target-manifest> <other-manifest>..."""
import json, sys

cost = json.load(open("/workspaces/MagicDeckTester/test/suite_cost.json"))["decks"]
F = {"ship": 1.0, "d3": 0.4, "d0": 0.005, "x3": 2.0, "xs": 5.0}


def w(j):
    nm = j["name"].replace("ident_", "")
    key, cell = nm.split("_")[0], nm.split("_")[1]
    return cost.get(key, {}).get("cost_per_game_ms", 200.0) * F.get(cell, 1.0)


tgt = json.load(open(sys.argv[1]))
names = {j["name"] for j in tgt["jobs"]}
for p in sys.argv[2:]:
    for j in json.load(open(p))["jobs"]:
        assert j["name"] not in names, j["name"]
        names.add(j["name"])
        tgt["jobs"].append(j)
tgt["jobs"].sort(key=lambda j: -w(j))
json.dump(tgt, open(sys.argv[1], "w"), indent=1)
print(len(tgt["jobs"]), sum(j["games"] for j in tgt["jobs"]))
