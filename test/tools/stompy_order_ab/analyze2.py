#!/usr/bin/env python3
"""Paired per-game analysis of the StompySurprise order A/B, over several result directories.

usage: analyze2.py <wins_dir> [<wins_dir> ...] [--slower-out FILE] [--arm-vs NEW:OLD ...]

Every <job>.wins under the directories is read; job = <deck>.<cell>.<tag>.c<off>. Games pair on
(deck, cell, gi). Tags: new (defaults), off (MTG_STOMPY_ORDER=0, the old arm), notop (order alone),
base (the base binary), id (identity decks). The same (deck, cell, tag, gi) seen in two directories must
agree, or it is reported. Loss = 9.
"""
import collections
import glob
import os
import sys

LOSS = 9
args = sys.argv[1:]
slower_out = None
pairs = [("new", "off"), ("notop", "off"), ("new", "notop")]
dirs = []
i = 0
while i < len(args):
    if args[i] == "--slower-out":
        slower_out = args[i + 1]; i += 2; continue
    if args[i] == "--arm-vs":
        a, b = args[i + 1].split(":"); pairs.append((a, b)); i += 2; continue
    dirs.append(args[i]); i += 1

wins = collections.defaultdict(dict)     # (deck, cell, tag, binary) -> gi -> (wt, digest)
units = collections.defaultdict(dict)
conflicts = 0
for d in dirs:
    binary = "base" if "wins_base" in d else "new"
    for p in glob.glob(os.path.join(d, "*.wins")):
        name = os.path.basename(p)[:-5]
        deck, cell, tag, _ = name.split(".")
        key = (deck, cell, tag, binary)
        for line in open(p):
            f = line.split()
            if len(f) < 2:
                continue
            gi, wt = int(f[0]), int(f[1])
            v = (LOSS if wt < 0 else wt, f[2] if len(f) > 2 else "")
            if gi in wins[key] and wins[key][gi] != v:
                conflicts += 1
            wins[key][gi] = v
        up = p[:-5] + ".units"
        if os.path.exists(up):
            for line in open(up):
                f = line.split()
                if len(f) >= 2:
                    units[key][int(f[0])] = int(f[1])


def arm(deck, cell, tag):
    b = "base" if tag in ("base",) else "new"
    return wins.get((deck, cell, tag, b), {}), units.get((deck, cell, tag, b), {})


print("loss = %d; paired per game; net = sum(A - B) turns (negative = A faster); conflicts=%d\n"
      % (LOSS, conflicts))
slower = []
for A, B in pairs:
    print("== %s vs %s ==" % (A, B))
    print("%-10s %-5s %6s %8s %8s %6s %6s %6s %7s %8s" %
          ("deck", "cell", "n", B, A, "fast", "slow", "net", "ident", "units"))
    for deck in ("stompy", "stompy2hg"):
        for cell in ("ship", "d3", "d0"):
            wa, ua = arm(deck, cell, A)
            wb, ub = arm(deck, cell, B)
            common = sorted(set(wa) & set(wb))
            if not common:
                continue
            fast = slow = net = ident = 0
            sa = sb = 0
            for gi in common:
                (xa, da), (xb, db) = wa[gi], wb[gi]
                sa += xa; sb += xb
                if xa < xb:
                    fast += 1
                elif xa > xb:
                    slow += 1
                    if (A, B) == ("new", "off"):
                        slower.append((deck, cell, gi, xb, xa))
                net += xa - xb
                if xa == xb and da == db:
                    ident += 1
            tua = sum(ua.get(g, 0) for g in common)
            tub = sum(ub.get(g, 0) for g in common)
            ur = ("%.3fx" % (tua / tub)) if tub else "-"
            print("%-10s %-5s %6d %8.4f %8.4f %6d %6d %+6d %7d %8s" %
                  (deck, cell, len(common), sb / len(common), sa / len(common), fast, slow, net, ident, ur))
    print()

print("== OFF SWITCH: new binary MTG_STOMPY_ORDER=0 vs the BASE binary, game for game ==")
for deck in ("stompy", "stompy2hg"):
    for cell in ("ship", "d3", "d0"):
        wo, _ = arm(deck, cell, "off")
        wb = wins.get((deck, cell, "base", "base"), {})
        common = sorted(set(wo) & set(wb))
        if not common:
            continue
        diff = [gi for gi in common if wo[gi] != wb[gi]]
        print("%-10s %-5s n=%5d differing=%d %s" % (deck, cell, len(common), len(diff), diff[:10]))

print("\n== IDENTITY: every other deck, base binary vs new binary at defaults ==")
bad = tot = 0
for (deck, cell, tag, binary), wb in sorted(wins.items()):
    if tag != "id" or binary != "base":
        continue
    wn = wins.get((deck, cell, "id", "new"), {})
    common = sorted(set(wb) & set(wn))
    diff = [gi for gi in common if wb[gi] != wn[gi]]
    tot += len(common)
    if diff or len(common) != len(wb):
        bad += 1
        print("  DIFF %-16s %-5s n=%d/%d differing=%s" % (deck, cell, len(common), len(wb), diff))
print("identity: %d games compared, %d (deck, cell) with a difference" % (tot, bad))

if slower_out:
    with open(slower_out, "w") as f:
        for s in slower:
            f.write("%s %s %d %d %d\n" % s)
    print("\n%d slower games (new vs off) -> %s" % (len(slower), slower_out))
