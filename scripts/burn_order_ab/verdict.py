#!/usr/bin/env python3
"""Adoption-bar verdict per arm per (deck, cell): net <= 0 (paired, loss = 9) AND every slower game recovers
at stage 1 (depth max(6, old wt), budget 100) or stage 2 (depth 8, budget 0).

  python3 verdict.py ANALYSIS_DIR [REC_DIR ...]
ANALYSIS_DIR holds analysis.txt + slower.json (make_ab.py --analyse); every REC_DIR's batch.err is searched
for the rec.* win lines. A slower game with no recovery result counts as UNCHECKED (the bar is not met until
it is checked)."""
import json, os, re, sys
from collections import defaultdict

ad = sys.argv[1]
recs = sys.argv[2:] or [ad]
WIN = re.compile(r"\[win\] job=(rec\.\S+) gi=(\d+) wt=(-?\d+)")
got = {}
for d in recs:
    p = os.path.join(d, "batch.err")
    if not os.path.exists(p):
        continue
    for line in open(p, errors="ignore"):
        m = WIN.search(line)
        if m:
            wt = int(m.group(3))
            got[m.group(1)] = 9 if wt < 0 else wt
rows = {}
for line in open(os.path.join(ad, "analysis.txt")):
    m = re.match(r"^(burn2hg|burn)\s+(ship|d3|d0)\s+(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(\d+)\s+([+-]\d+)\s+(\S+)", line)
    if m:
        rows[(m.group(1), m.group(2), m.group(3))] = dict(n=int(m.group(4)), fast=int(m.group(7)),
                                                          slow=int(m.group(8)), net=int(m.group(9)),
                                                          units=m.group(10))
slower = json.load(open(os.path.join(ad, "slower.json")))
rec = defaultdict(lambda: [0, 0, 0, 0])   # s1, s2, none, unchecked
bad = defaultdict(list)
for deck, cell, arm, g, ob, nw in slower:
    k1 = "rec.%s.%s.%s.g%d.s1" % (deck, cell, arm, g)
    k2 = "rec.%s.%s.%s.g%d.s2" % (deck, cell, arm, g)
    t = rec[(deck, cell, arm)]
    if k1 not in got and k2 not in got:
        t[3] += 1
        bad[(deck, cell, arm)].append("gi=%d unchecked" % g)
        continue
    if got.get(k1, 99) <= ob:
        t[0] += 1
    elif got.get(k2, 99) <= ob:
        t[1] += 1
    else:
        t[2] += 1
        bad[(deck, cell, arm)].append("gi=%d old=%d s1=%s s2=%s" % (g, ob, got.get(k1), got.get(k2)))
arms = sorted({k[2] for k in rows})
print("%-8s %-5s %-6s %6s %5s %5s %5s %7s  %4s %4s %4s %4s  %s" % ("deck", "cell", "arm", "n", "fast", "slow",
      "net", "units", "s1", "s2", "none", "unch", "bar"))
for arm in arms:
    ok_all = True
    for deck in ("burn", "burn2hg"):
        for cell in ("ship", "d3", "d0"):
            r = rows.get((deck, cell, arm))
            if r is None:
                continue
            t = rec[(deck, cell, arm)]
            ok = r["net"] <= 0 and t[2] == 0 and t[3] == 0
            ok_all = ok_all and ok
            print("%-8s %-5s %-6s %6d %5d %5d %+5d %7s  %4d %4d %4d %4d  %s" % (
                deck, cell, arm, r["n"], r["fast"], r["slow"], r["net"], r["units"], t[0], t[1], t[2], t[3],
                "PASS" if ok else "FAIL"))
    print("%-21s ARM %s" % (arm, "MEETS THE BAR" if ok_all else "fails the bar"))
    print()
print("== not recovered / unchecked ==")
for k in sorted(bad):
    for b in bad[k][:40]:
        print("  %s %s %s  %s" % (k[0], k[1], k[2], b))
