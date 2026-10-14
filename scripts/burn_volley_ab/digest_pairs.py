#!/usr/bin/env python3
"""Pairwise: in how many job chunks do two arms' play digests differ? Usage: digest_pairs.py <ab_dir> a:b ..."""
import os, re, sys
from collections import defaultdict

pat = re.compile(r"^burn_(ship|d3|d0)_(old|new)_([a-z0-9]+)\.c(\d+): played=(\d+) avg=\S+ digest=(\S+)")
dig = defaultdict(dict)
for line in open(os.path.join(sys.argv[1], "batch.out"), errors="ignore"):
    m = pat.search(line)
    if m:
        dig[(m.group(1), m.group(3))][(m.group(2), int(m.group(4)))] = m.group(6)
for pair in sys.argv[2:]:
    a, b = pair.split(":")
    row = []
    for cell in ("ship", "d3", "d0"):
        da, db = dig.get((cell, a), {}), dig.get((cell, b), {})
        common = [k for k in da if k in db]
        row.append("%s %d/%d" % (cell, sum(1 for k in common if da[k] != db[k]), len(common)))
    print("%-16s %s" % (pair, "  ".join(row)))
