#!/usr/bin/env python3
"""Paired analysis of the payment fix's movers (fix vs off, both on the fix binary): the v4 screen (gi 0..149,
<v4>/wins_new) pooled with the v5 expansion (gi 150.., <v5>/wins). Loss = 9.
usage: analyze_v5.py <v4_dir> <v5_dir> [--slower-out FILE]
--slower-out writes the slower games (deck cell gi old new) for a recovery run."""
import collections
import glob
import os
import sys

LOSS = 9
v4, v5 = sys.argv[1], sys.argv[2]
pre = sys.argv[sys.argv.index("--slower-out") + 1] if "--slower-out" in sys.argv else None
DECKS = ("selesnya", "fivecolour", "bruna")
wins = collections.defaultdict(dict)
units = collections.defaultdict(dict)
for p in glob.glob(os.path.join(v4, "wins_new", "*.wins")) + glob.glob(os.path.join(v5, "wins", "*.wins")):
    parts = os.path.basename(p)[:-5].split(".")
    if len(parts) != 4 or parts[0] not in DECKS or parts[2] not in ("off", "fix"):
        continue
    deck, cell, tag, _ = parts
    k = (deck, cell, tag)
    for line in open(p):
        f = line.split()
        if len(f) >= 2:
            wt = int(f[1])
            wins[k][int(f[0])] = (LOSS if wt < 0 else wt, f[2] if len(f) > 2 else "")
    up = p[:-5] + ".units"
    if os.path.exists(up):
        for line in open(up):
            f = line.split()
            if len(f) >= 2:
                units[k][int(f[0])] = int(f[1])
print("loss = %d; fix vs off, paired per game; net = sum(fix - off)\n" % LOSS)
print("%-11s %-5s %6s %8s %8s %5s %5s %6s %6s %8s" % ("deck", "cell", "n", "off", "fix", "fast", "slow", "net",
                                                       "ident", "units"))
slower = []
for deck in DECKS:
    for cell in ("ship", "d3", "d0"):
        wo, wf = wins.get((deck, cell, "off"), {}), wins.get((deck, cell, "fix"), {})
        uo, uf = units.get((deck, cell, "off"), {}), units.get((deck, cell, "fix"), {})
        common = sorted(set(wo) & set(wf))
        if not common:
            continue
        fast = slow = net = ident = 0
        for gi in common:
            (xo, do), (xf, df) = wo[gi], wf[gi]
            if xf < xo:
                fast += 1
            elif xf > xo:
                slow += 1
                slower.append((deck, cell, gi, xo, xf))
            net += xf - xo
            if xf == xo and do == df:
                ident += 1
        to = sum(uo.get(g, 0) for g in common)
        tf = sum(uf.get(g, 0) for g in common)
        print("%-11s %-5s %6d %8.4f %8.4f %5d %5d %+6d %6d %8s" % (
            deck, cell, len(common), sum(wo[g][0] for g in common) / len(common),
            sum(wf[g][0] for g in common) / len(common), fast, slow, net, ident,
            ("%.3fx" % (tf / to)) if to else "-"))
print("\nslower games (deck cell gi off fix):")
for s in slower:
    print("  %s %s gi%d %d->%d" % s)
if pre:
    with open(pre, "w") as fh:
        for s in slower:
            fh.write("%s %s %d %d %d\n" % s)
