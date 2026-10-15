#!/usr/bin/env python3
"""What the rebase alone moved: one arm's per-game win turns on the OLD binary's A/B dirs vs the NEW one's,
same seeds. cmp_bases.py <arm> <new-dir> <old-dir>...   (loss = 9)"""
import os, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ab import load, KEYS, CELL_IX  # noqa: E402

arm, new = sys.argv[1], sys.argv[2]
old = defaultdict(dict)
for d in sys.argv[3:]:
    w, _ = load(d)
    for kc, arms in w.items():
        old[kc].update(arms.get(arm, {}))
nw, _ = load(new)
for kc in sorted(nw, key=lambda kc: (KEYS.index(kc[0]) if kc[0] in KEYS else 99, CELL_IX[kc[1]])):
    a, b = old.get(kc, {}), nw[kc].get(arm, {})
    gis = sorted(set(a) & set(b))
    if not gis:
        continue
    diff = [(g, a[g], b[g]) for g in gis if a[g] != b[g]]
    print("%-16s %-4s n=%4d  differ=%3d  net(new-old)=%+d  %s" % (
        kc[0], kc[1], len(gis), len(diff), sum(x[2] - x[1] for x in diff),
        " ".join("gi%d:%d->%d" % x for x in diff[:12])))
