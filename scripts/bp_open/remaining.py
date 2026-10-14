#!/usr/bin/env python3
"""Which jobs of a pooled batch have not finished, grouped by (cell, arm). remaining.py <dir>"""
import json, os, re, sys
from collections import Counter

d = sys.argv[1]
m = json.load(open(os.path.join(d, "manifest.json")))
done = set()
for line in open(os.path.join(d, "batch.out"), errors="ignore"):
    mm = re.match(r"^(\S+): played=", line)
    if mm:
        done.add(mm.group(1))
c = Counter()
g = Counter()
for j in m["jobs"]:
    if j["name"] in done:
        continue
    nm = j["name"]
    parts = nm.replace("ident_", "ident-").split("_")
    c[(parts[1], parts[2].split(".")[0])] += 1
    g[(parts[1], parts[2].split(".")[0])] += j["games"]
for k in sorted(c):
    print(k, c[k], "jobs", g[k], "games")
ship_left = sorted({j["name"].split("_")[0] for j in m["jobs"] if j["name"] not in done and "_ship_" in j["name"]})
print("decks with ship jobs left:", ",".join(ship_left))
