#!/usr/bin/env python3
"""Per-deck report of the wavebp hatch A/B: wavebp_ab_report.py <ab.out> <ab.err> [max_turns_fold]

Pairs games by (deck key, gi) from the `[win] job=<arm>__<key>_c<c> gi=.. wt=..` lines (MTG_DUMP_WINS=1); an unwon
game (wt <= 0) scores max_turns+1 (the suite's loss-penalized metric; default fold 9 = max_turns 8 + 1 is printed when used). Prints per
deck: games, better/worse/same, net mean delta (new - base, lower = better), summed job ms and units per arm and
ms/game, then every slower game with its seed (BASE + gi) for the d8 b0 check.
"""
import collections
import re
import sys

BASE = 9600001
fold = int(sys.argv[3]) if len(sys.argv) > 3 else 9
WIN = re.compile(r"^\[win\] job=(\w+)__(\S+)_c\d+ gi=(\d+) wt=(-?\d+)")
JOB = re.compile(r"^(\w+)__(\S+)_c\d+: played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+)")
wt = collections.defaultdict(dict)
cost = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0, 0]))
for line in open(sys.argv[2], errors="replace"):
    m = WIN.match(line)
    if m:
        arm, key, gi, w = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
        wt[key][(arm, gi)] = w if w > 0 else fold
for line in open(sys.argv[1], errors="replace"):
    m = JOB.match(line.strip())
    if m:
        c = cost[m.group(2)][m.group(1)]
        c[0] += int(m.group(3)); c[1] += int(m.group(4)); c[2] += int(m.group(5))
slow = []
print("%-20s %5s %4s %4s %5s %8s %9s %9s %7s %12s %12s" % ("deck", "games", "bet", "wor", "same", "net", "ms/g base", "ms/g new",
                                                           "ms x", "units base", "units new"))
for key in sorted(wt):
    d = wt[key]
    gis = sorted({gi for (a, gi) in d if ("base", gi) in d and ("new", gi) in d})
    bet = sum(1 for g in gis if d[("new", g)] < d[("base", g)])
    wor = [g for g in gis if d[("new", g)] > d[("base", g)]]
    net = sum(d[("new", g)] - d[("base", g)] for g in gis) / max(1, len(gis))
    cb, cn = cost[key]["base"], cost[key]["new"]
    msb = cb[1] / max(1, cb[0]); msn = cn[1] / max(1, cn[0])
    print("%-20s %5d %4d %4d %5d %+8.4f %9.0f %9.0f %7.3f %12d %12d" % (key, len(gis), bet, len(wor), len(gis) - bet - len(wor),
                                                                     net, msb, msn, msn / msb if msb else 0, cb[2], cn[2]))
    for g in wor:
        slow.append((key, g, d[("base", g)], d[("new", g)]))
print("\nslower games (key gi seed base->new):")
for key, g, b, n in slow:
    print("  %s gi%d seed %d: %d -> %d" % (key, g, BASE + g, b, n))
