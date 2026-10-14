#!/usr/bin/env python3
"""Append the identity jobs (renamed ident_*) to a manifest and re-sort expensive-first.
  merge_ident.py <target-manifest> <ident-manifest>"""
import json, sys

COST = {"snow": 7.2, "bruna": 4.4, "hinata": 1.7, "pd": 1.5, "fivecolour": 1.2, "kittyv2": 1.0,
        "melira": 1.0, "fungusb": 0.56, "kitty": 0.34, "fungus": 0.32}


def w(j):
    parts = j["name"].replace("ident_", "").split("_")
    key, cell = parts[0], parts[1]
    if cell == "d0":
        return 0.01
    return COST.get(key, 0.2) * (1.0 if cell == "ship" else 0.4)


tgt, idp = sys.argv[1], sys.argv[2]
m = json.load(open(tgt))
idm = json.load(open(idp))
have = {j["name"] for j in m["jobs"]}
for j in idm["jobs"]:
    k = dict(j)
    k["name"] = "ident_" + j["name"]
    if k["name"] not in have:
        m["jobs"].append(k)
m["jobs"].sort(key=lambda j: -w(j))
json.dump(m, open(tgt, "w"), indent=1)
print(len(m["jobs"]), sum(j["games"] for j in m["jobs"]))
