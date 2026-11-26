#!/usr/bin/env python3
"""Per-deck units/game, ms/game and digest agreement of two batch outputs of the same manifest.

    units_report.py <base.out> <new.out>
"""
import re
import sys
from collections import defaultdict

LINE = re.compile(r"^(\S+?)__(\S+)_c(\d+): played=(\d+) avg=([\d.]+) digest=(\S+) ms=(\d+) units=(\d+)")


def load(path):
    out = {}
    for ln in open(path):
        m = LINE.match(ln.strip())
        if m:
            out[(m.group(2), int(m.group(3)))] = (int(m.group(4)), float(m.group(5)), m.group(6), int(m.group(7)), int(m.group(8)))
    return out


def main():
    b, n = load(sys.argv[1]), load(sys.argv[2])
    agg = defaultdict(lambda: [0, 0, 0, 0, 0, 0, 0.0, 0.0, 0])
    for k in sorted(set(b) & set(n)):
        a = agg[k[0]]
        pb, avb, db, msb, ub = b[k]
        pn, avn, dn, msn, un = n[k]
        a[0] += pb; a[1] += ub; a[2] += un; a[3] += msb; a[4] += msn
        a[5] += 1 if db == dn else 0
        a[6] += avb * pb; a[7] += avn * pn; a[8] += 1
    print("%-20s %6s %13s %13s %7s %9s %9s %10s %9s" % ("deck", "games", "units base", "units new", "new/b",
                                                         "ms/g b", "ms/g n", "dig==", "net turn"))
    for d, a in sorted(agg.items()):
        print("%-20s %6d %13d %13d %7.3f %9.0f %9.0f %6d/%-3d %+9.4f" % (d, a[0], a[1], a[2], a[2] / max(1, a[1]),
              a[3] / a[0], a[4] / a[0], a[5], a[8], (a[7] - a[6]) / a[0]))


if __name__ == "__main__":
    main()
