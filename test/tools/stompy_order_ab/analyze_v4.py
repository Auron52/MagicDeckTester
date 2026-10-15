#!/usr/bin/env python3
"""Paired analysis of the v4 A/B (order + payment fix). usage: analyze_v4.py <dir> [--slower-out PREFIX]

<dir>/wins_new and <dir>/wins_base hold <deck>.<cell>.<tag>.c<off>.wins (+ .units). Loss = 9.
Prints, per (deck, cell): every comparison arm-vs-off on the new binary, and base(binary) vs off.
--slower-out PREFIX writes PREFIX.<arm>.txt: the slower games of <arm> vs off (deck cell gi old new).
"""
import collections
import glob
import os
import sys

LOSS = 9
d = sys.argv[1]
pre = sys.argv[sys.argv.index("--slower-out") + 1] if "--slower-out" in sys.argv else None
wins = collections.defaultdict(dict)
units = collections.defaultdict(dict)
for binary in ("new", "base"):
    for p in glob.glob(os.path.join(d, "wins_" + binary, "*.wins")):
        deck, cell, tag, _ = os.path.basename(p)[:-5].split(".")
        k = (deck, cell, tag, binary)
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

decks = sorted(set(k[0] for k in wins), key=lambda x: (not x.startswith("stompy"), x))
CELLS = ("ship", "d3", "d0")


def compare(wa, wb, ua, ub):
    common = sorted(set(wa) & set(wb))
    if not common:
        return None
    fast = slow = net = ident = 0
    sa = sb = 0
    slowg = []
    for gi in common:
        (xa, da), (xb, db) = wa[gi], wb[gi]
        sa += xa; sb += xb
        if xa < xb:
            fast += 1
        elif xa > xb:
            slow += 1; slowg.append((gi, xb, xa))
        net += xa - xb
        if xa == xb and da == db:
            ident += 1
    tua = sum(ua.get(g, 0) for g in common); tub = sum(ub.get(g, 0) for g in common)
    return dict(n=len(common), a=sa / len(common), b=sb / len(common), fast=fast, slow=slow, net=net,
                ident=ident, ur=(tua / tub) if tub else None, slowg=slowg)


slower = collections.defaultdict(list)
print("loss = %d; paired per game vs the new binary's OFF arm (no order, no fix); net = sum(arm - off)\n" % LOSS)
print("%-16s %-5s %-7s %6s %8s %8s %5s %5s %6s %6s %8s" %
      ("deck", "cell", "arm", "n", "off", "arm", "fast", "slow", "net", "ident", "units"))
for deck in decks:
    for cell in CELLS:
        off = wins.get((deck, cell, "off", "new"), {})
        uoff = units.get((deck, cell, "off", "new"), {})
        for tag in ("fix", "order", "ofix", "onotop"):
            w = wins.get((deck, cell, tag, "new"))
            if not w:
                continue
            r = compare(w, off, units.get((deck, cell, tag, "new"), {}), uoff)
            if r is None:
                continue
            ur = "%.3fx" % r["ur"] if r["ur"] else "-"
            flag = "" if r["ident"] == r["n"] else ("  MOVES" if tag == "fix" else "")
            print("%-16s %-5s %-7s %6d %8.4f %8.4f %5d %5d %+6d %6d %8s%s" %
                  (deck, cell, tag, r["n"], r["b"], r["a"], r["fast"], r["slow"], r["net"], r["ident"], ur, flag))
            for gi, o, nw in r["slowg"]:
                slower[tag].append((deck, cell, gi, o, nw))

print("\n== IDENTITY: base binary vs the new binary's off arm, game for game ==")
bad = tot = 0
for deck in decks:
    for cell in CELLS:
        b = wins.get((deck, cell, "base", "base"), {})
        o = wins.get((deck, cell, "off", "new"), {})
        common = sorted(set(b) & set(o))
        if not common:
            continue
        diff = [gi for gi in common if b[gi] != o[gi]]
        tot += len(common)
        if diff:
            bad += 1
            print("  DIFF %-16s %-5s n=%d differing=%s" % (deck, cell, len(common), diff[:12]))
print("identity: %d games compared, %d (deck, cell) with a difference" % (tot, bad))

if pre:
    for tag, rows in slower.items():
        with open("%s.%s.txt" % (pre, tag), "w") as f:
            for r in rows:
                f.write("%s %s %d %d %d\n" % r)
    print("\nslower lists: " + ", ".join("%s=%d" % (t, len(r)) for t, r in sorted(slower.items())))
