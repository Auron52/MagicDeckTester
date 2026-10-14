#!/usr/bin/env python3
"""Rough ETA of a pooled batch: per-job weight from test/suite_cost.json, calibrated on finished jobs.
  eta.py <dir> [threads]"""
import json, os, re, sys

d = sys.argv[1]
threads = int(sys.argv[2]) if len(sys.argv) > 2 else 6
cost = json.load(open("/workspaces/MagicDeckTester/test/suite_cost.json"))["decks"]
m = json.load(open(os.path.join(d, "manifest.json")))
done = {}
for line in open(os.path.join(d, "batch.out"), errors="ignore"):
    mm = re.match(r"^(\S+): played=(\d+) .* ms=(\d+)", line)
    if mm:
        done[mm.group(1)] = int(mm.group(3))


def w(j):
    nm = j["name"].replace("ident_", "")
    key, cell = nm.split("_")[0], nm.split("_")[1]
    base = cost.get(key, {}).get("cost_per_game_ms", 200.0)
    f = {"ship": 1.0, "d3": 0.4, "d0": 0.005, "x3": 2.0, "xs": 5.0}.get(cell, 1.0)
    return base * f * j["games"]


wd = sum(w(j) for j in m["jobs"] if j["name"] in done)
ms_done = sum(done.values())
wr = sum(w(j) for j in m["jobs"] if j["name"] not in done)
scale = ms_done / wd if wd else 1.0
print("done %d/%d jobs, %.0f core-s; remaining est %.0f core-s -> %.0f min at %d threads (scale %.2f)" % (
    len(done), len(m["jobs"]), ms_done / 1000, wr * scale / 1000, wr * scale / 1000 / threads / 60, threads, scale))
