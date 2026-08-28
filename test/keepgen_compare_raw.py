#!/usr/bin/env python3
"""Structural comparison of two keepgen raw sidecars.

`cmp` is the wrong gate here: two UNINTERRUPTED runs of the same binary already differ on a deck of
this size, because compute_refs' reconcile reads S7.cnt at whatever the in-flight state happens to
be when the sub-refine converges. What is actually claimed -- and what has to hold -- is the
property docs/design/keepgen-resume-exactness.md calls "same-count-different-value == 0": a rollout
is a pure function of (seed_base, cell, pd, r), so a cell's sum is fully determined by its final
count. Any cell where both runs reached the SAME count but carry a DIFFERENT sum is a real defect;
a cell that merely ended at a different count is schedule drift.

Usage: compare_raw.py A.raw.json B.raw.json
"""
import json, sys
from collections import defaultdict

def load(p):
    r = json.load(open(p))
    cells = {}
    for s in r["sizes"]:
        H = s["H"]
        for e in s["entries"]:
            key = (H, tuple(e["comp"]))
            for pd in (0, 1):
                cells[(key, pd)] = (e["count"][pd], e["sum"][pd], e["sumsq"][pd])
    return r["meta"], cells

ma, A = load(sys.argv[1])
mb, B = load(sys.argv[2])

only_a = set(A) - set(B)
only_b = set(B) - set(A)
more = fewer = equal = 0
badval = []
per_H = defaultdict(lambda: [0, 0])   # H -> [differing cells, total cells]
tot_a = tot_b = 0
for k in set(A) & set(B):
    ca, sa, qa = A[k]
    cb, sb, qb = B[k]
    tot_a += ca; tot_b += cb
    H = k[0][0]
    per_H[H][1] += 1
    if ca == cb:
        equal += 1
        if sa != sb or qa != qb:
            badval.append((k, A[k], B[k]))
            per_H[H][0] += 1
    else:
        per_H[H][0] += 1
        if cb > ca: more += 1
        else:       fewer += 1

print(f"file A: {sys.argv[1]}")
print(f"file B: {sys.argv[2]}")
print(f"  play_digest  A={ma.get('play_digest')!r}  B={mb.get('play_digest')!r}")
print(f"  cell-sides   {len(A)} vs {len(B)}   (A-only {len(only_a)}, B-only {len(only_b)})")
print(f"  rollouts     {tot_a} -> {tot_b}   ({(tot_b-tot_a)/max(1,tot_a)*100:+.3f}%)")
print(f"  counts       more(B) {more} / fewer(B) {fewer} / equal {equal}")
print(f"  SAME-COUNT-DIFFERENT-VALUE: {len(badval)}   <-- must be 0")
for k, a, b in badval[:5]:
    print(f"     {k}  A={a}  B={b}")
print("  per hand size (differing / total):")
for H in sorted(per_H, reverse=True):
    d, t = per_H[H]
    print(f"     H={H}: {d} / {t}")
sys.exit(1 if badval or only_a or only_b else 0)
