#!/usr/bin/env python3
"""The proposed MTG_CRITTER_HELIOD_FIRST amendment, measured against the order as settled (ONE pooled batch).

  python3 -I make_ab3.py build OUT        arms on the amendment binary: `new` (default, amendment OFF) and `hf`
                                          (MTG_CRITTER_HELIOD_FIRST=1), every critter / critter2hg cell at the
                                          A/B's sizes and seeds
  python3 -I make_ab3.py analyse OUT A_DIR  hf vs new per cell + `new` here vs batch A's `new` (identity: the
                                          amendment binary with the lever off plays exactly af7434cc's order)
"""
import json, os, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab as M
import make_ab2 as M2

CELLS = [("critter", "ship", 4000), ("critter2hg", "ship", 2000), ("critter", "d3", 6000), ("critter2hg", "d3", 3000),
         ("critter", "d0", 20000), ("critter2hg", "d0", 10000)]


def build(out):
    jobs = []
    for key, cell, n in CELLS:
        jobs += M2.critter_jobs(key, cell, n, "new", None)
        jobs += M2.critter_jobs(key, cell, n, "hf", {"MTG_CRITTER_HELIOD_FIRST": True})
    jobs.sort(key=lambda j: 0 if j.get("depth", 5) > 0 else 1)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s/manifest.json: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


def analyse(out, adir):
    w, j = M.load(out)
    P, C, D = M2.per_arm(w, j)
    wa, ja = M.load(adir)
    PA, CA, DA = M2.per_arm(wa, ja)
    print("AMENDMENT: hf (MTG_CRITTER_HELIOD_FIRST=1) vs new (the settled order), paired; unwon = 9")
    print("%-11s %-5s %6s %8s %8s %9s %7s %6s %6s %6s %8s" % ("deck", "cell", "n", "new", "hf", "hf-new", "t",
                                                            "faster", "slower", "net", "units x"))
    slower = []
    net = defaultdict(int)
    for key, cell, n in CELLS:
        r = M2.paired(P[(key, cell, "hf")], P[(key, cell, "new")])
        if r is None:
            print("%-11s %-5s (no data)" % (key, cell))
            continue
        cn, co = C[(key, cell, "hf")], C[(key, cell, "new")]
        net[key] += r["net"]
        print("%-11s %-5s %6d %8.4f %8.4f %+9.5f %7.2f %6d %6d %+6d %8s" % (
            key, cell, r["n"], r["b"], r["a"], r["mean"], r["t"], r["faster"], r["slower"], r["net"],
            ("%.3f" % (cn[1] / co[1])) if co[1] else "-"))
        for g in r["gis"]:
            if P[(key, cell, "hf")][g] > P[(key, cell, "new")][g]:
                slower.append((key, cell, g, P[(key, cell, "new")][g], P[(key, cell, "hf")][g],
                               11200001 + 100000 * (0 if key == "critter" else 1) + 30000 * M.CELL_IX[cell] + g))
    print("\nPER-DECK NET (hf - new): " + ", ".join("%s %+d" % kv for kv in net.items()))
    tot = same = 0
    for (ident, key, cell, arm, c), v in D.items():
        if arm != "new":
            continue
        x = DA.get((False, key, cell, "new", c))
        if x is None:
            continue
        tot += 1
        same += (x[0] == v[0])
    print("IDENTITY: amendment binary, lever off, vs batch A's new arm: %d/%d chunk digests identical" % (same, tot))
    print("\nEVERY hf SLOWER game (deck cell gi new hf seed):")
    for s in slower:
        print("  %-11s %-5s gi=%-6d new=%d hf=%d seed=%d" % s)
    json.dump({"slower": slower}, open(os.path.join(out, "moved.json"), "w"))


if __name__ == "__main__":
    if sys.argv[1] == "build":
        build(sys.argv[2])
    else:
        analyse(sys.argv[2], sys.argv[3])
