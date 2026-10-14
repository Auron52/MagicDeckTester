#!/usr/bin/env python3
"""Write the unfinished jobs of a batch as a new manifest, keeping every ship and identity job and only
the first `d3_games` / `d0_games` of the on/off arms of d3 / d0 (the movers question needs digests,
not power). shrink_remaining.py <dir> <out-dir> [d3_games] [d0_games]"""
import json, os, re, sys

d, out = sys.argv[1], sys.argv[2]
d3g = int(sys.argv[3]) if len(sys.argv) > 3 else 100
d0g = int(sys.argv[4]) if len(sys.argv) > 4 else 50
m = json.load(open(os.path.join(d, "manifest.json")))
done = set()
for line in open(os.path.join(d, "batch.out"), errors="ignore"):
    mm = re.match(r"^(\S+): played=", line)
    if mm:
        done.add(mm.group(1))
keep = []
for j in m["jobs"]:
    nm = j["name"]
    if nm in done:
        continue
    if nm.startswith("ident_"):
        keep.append(j); continue
    mm = re.match(r"^(\S+?)_(ship|d3|d0)_([a-z0-9]+)\.c(\d+)$", nm)
    cell, arm, c = mm.group(2), mm.group(3), int(mm.group(4))
    if cell == "ship":
        keep.append(j)
    elif arm in ("on", "off") and c < (d3g if cell == "d3" else d0g):
        keep.append(j)
os.makedirs(out, exist_ok=True)
json.dump({"jobs": keep}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
json.dump(json.load(open(os.path.join(d, "seeds.json"))), open(os.path.join(out, "seeds.json"), "w"))
print("%d jobs, %d games" % (len(keep), sum(j["games"] for j in keep)))
