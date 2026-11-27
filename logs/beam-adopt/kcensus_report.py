#!/usr/bin/env python3
"""K census report: per deck, K-off vs K-on job (same seed): units ratio, avg, digest equality. usage: <dir>"""
import re, sys, os
J = {}
for ln in open(os.path.join(sys.argv[1], "batch.out")):
    m = re.match(r"^(\S+)_(K[01])_s(\d+): played=(\d+) avg=(\S+) digest=(\S+) ms=(\d+) units=(\d+)", ln)
    if m:
        J.setdefault(m.group(1), {})[m.group(2)] = (float(m.group(5)), m.group(6), int(m.group(8)), int(m.group(7)))
moved = []
for row, a in sorted(J.items()):
    if "K0" in a and "K1" in a:
        (a0, d0, u0, m0), (a1, d1, u1, m1) = a["K0"], a["K1"]
        tag = "ARMED" if u0 != u1 else ("digest" if d0 != d1 else "same")
        if tag != "same":
            moved.append(row.split("_d")[0])
        print(f"{row:28s} {tag:6s} units x{u1 / u0:.3f}  avg {a0:.3f} -> {a1:.3f}  ms/g {m0/20:.0f}")
print("moved:", ",".join(moved))
