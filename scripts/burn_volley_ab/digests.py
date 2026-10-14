#!/usr/bin/env python3
"""Per cell and arm: how many job chunks have a play digest that differs from the cur arm's same chunk
(digest = the job's whole-play fingerprint in batch.out). Usage: digests.py <ab_dir>"""
import os, re, sys
from collections import defaultdict

pat = re.compile(r"^burn_(ship|d3|d0)_(old|new)_([a-z0-9]+)\.c(\d+): played=(\d+) avg=\S+ digest=(\S+)")
dig = defaultdict(dict)
for line in open(os.path.join(sys.argv[1], "batch.out"), errors="ignore"):
    m = pat.search(line)
    if m:
        dig[(m.group(1), m.group(2), m.group(3))][int(m.group(4))] = m.group(6)
for vs in ("cur", "generic"):
    print("== chunks whose digest differs from %s (differ / compared) ==" % vs)
    for cell in ("ship", "d3", "d0"):
        for sset in ("old", "new"):
            base = dig.get((cell, sset, vs), {})
            row = []
            for (c, s, arm), d in sorted(dig.items()):
                if c != cell or s != sset or arm == vs:
                    continue
                common = [k for k in d if k in base]
                row.append("%s %d/%d" % (arm, sum(1 for k in common if d[k] != base[k]), len(common)))
            if row:
                print("  %-4s %-3s %s" % (cell, sset, "  ".join(row)))
