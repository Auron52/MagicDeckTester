#!/usr/bin/env python3
"""Compare two ref_bench text reports game by game (A = defaults, B = levers).
usage: refs_cmp.py <A.txt> <B.txt>
Prints per deck: n, human / A / B averages (no-win = max_turns + 1, ref_bench's own score), shortfall counts,
and every game whose search win turn moved."""
import re
import sys

ROW = re.compile(r"^(\S+)\s+(\S+)\s+(\S+) \|\s+(\S+)\s+(\S+) \|(.*)$")
HEAD = re.compile(r"^=== (\S+)\s+\(n=(\d+), max_turns=(\d+), no-win scores (\d+)\)")


def parse(path):
    out, deck, loss = {}, None, 10
    for line in open(path):
        m = HEAD.match(line)
        if m:
            deck, loss = m.group(1), int(m.group(4))
            out[deck] = {}
            continue
        m = ROW.match(line.rstrip("\n"))
        if not m or deck is None or m.group(1) in ("reference", "AVG"):
            continue
        sc = lambda v: int(v) if v.isdigit() else loss
        out[deck][(m.group(1), m.group(2), m.group(3))] = (sc(m.group(4)), sc(m.group(5)), m.group(6).strip())
    return out


A, B = parse(sys.argv[1]), parse(sys.argv[2])
tn = tmoved = 0
tnet = 0
for deck in sorted(set(A) & set(B)):
    keys = sorted(set(A[deck]) & set(B[deck]))
    if not keys:
        continue
    h = sum(A[deck][k][0] for k in keys)
    sa = sum(A[deck][k][1] for k in keys)
    sb = sum(B[deck][k][1] for k in keys)
    shA = sum(1 for k in keys if "SHORTFALL" in A[deck][k][2])
    shB = sum(1 for k in keys if "SHORTFALL" in B[deck][k][2])
    moved = [k for k in keys if A[deck][k][1] != B[deck][k][1]]
    print("%-38s n=%3d human=%.3f A=%.3f B=%.3f net(B-A)=%+d short A=%d B=%d" % (
        deck, len(keys), h / len(keys), sa / len(keys), sb / len(keys), sb - sa, shA, shB))
    for k in moved:
        print("    %s seed %s gi %s: A %d -> B %d (human %d)" % (k[0], k[1], k[2], A[deck][k][1], B[deck][k][1],
                                                              A[deck][k][0]))
    tn += len(keys)
    tnet += sb - sa
    tmoved += len(moved)
print("ALL n=%d net(B-A)=%+d moved=%d" % (tn, tnet, tmoved))
