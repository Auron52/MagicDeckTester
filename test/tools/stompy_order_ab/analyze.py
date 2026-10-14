#!/usr/bin/env python3
"""Paired per-game analysis of the StompySurprise order A/B (2026-10-14).

usage: analyze.py <base_wins_dir> <new_wins_dir> [--slower-out FILE]

Job names are <deck>.<cell>.<tag>.c<off>; .wins lines are `<gi> <win_turn> <digest>` (-1 = unwon,
scored max_turns+1 = 9). Pairs on (deck, cell, gi).
"""
import collections
import glob
import os
import sys

LOSS = 9
base_dir, new_dir = sys.argv[1], sys.argv[2]
slower_out = None
if "--slower-out" in sys.argv:
    slower_out = sys.argv[sys.argv.index("--slower-out") + 1]


def load(d):
    wins = collections.defaultdict(dict)    # (deck, cell, tag) -> gi -> (wt, digest)
    units = collections.defaultdict(dict)
    for p in glob.glob(os.path.join(d, "*.wins")):
        name = os.path.basename(p)[:-5]
        deck, cell, tag, _ = name.split(".")
        for line in open(p):
            f = line.split()
            if len(f) < 2:
                continue
            gi, wt = int(f[0]), int(f[1])
            wins[(deck, cell, tag)][gi] = (LOSS if wt < 0 else wt, f[2] if len(f) > 2 else "")
        up = p[:-5] + ".units"
        if os.path.exists(up):
            for line in open(up):
                f = line.split()
                if len(f) >= 2:
                    units[(deck, cell, tag)][int(f[0])] = int(f[1])
    return wins, units


bw, bu = load(base_dir)
nw, nu = load(new_dir)

print("loss = %d; paired per game; net = sum(new - old) turns (negative = new faster)\n" % LOSS)
print("%-10s %-5s %6s %8s %8s %6s %6s %6s %7s %9s" %
      ("deck", "cell", "n", "old", "new", "fast", "slow", "net", "ident", "units"))
slower = []
for deck in ("stompy", "stompy2hg"):
    for cell in ("ship", "d3", "d0"):
        o = bw.get((deck, cell, "base"), {})
        n = nw.get((deck, cell, "new"), {})
        common = sorted(set(o) & set(n))
        if not common:
            continue
        fast = slow = net = ident = 0
        so = sn = 0
        for gi in common:
            (wo, do), (wn, dn) = o[gi], n[gi]
            so += wo; sn += wn
            if wn < wo:
                fast += 1
            elif wn > wo:
                slow += 1
                slower.append((deck, cell, gi, wo, wn))
            net += wn - wo
            if wn == wo and do == dn:
                ident += 1
        uo = sum(bu.get((deck, cell, "base"), {}).get(g, 0) for g in common)
        un = sum(nu.get((deck, cell, "new"), {}).get(g, 0) for g in common)
        ur = ("%.4f" % (un / uo)) if uo else "-"
        print("%-10s %-5s %6d %8.4f %8.4f %6d %6d %+6d %7d %9s" %
              (deck, cell, len(common), so / len(common), sn / len(common), fast, slow, net, ident, ur))

print("\n== OFF switch (new binary, MTG_STOMPY_ORDER=0) vs the base binary, same games ==")
for deck in ("stompy", "stompy2hg"):
    for cell in ("ship", "d3", "d0"):
        o = bw.get((deck, cell, "base"), {})
        n = nw.get((deck, cell, "off"), {})
        common = sorted(set(o) & set(n))
        if not common:
            continue
        diff = [gi for gi in common if o[gi] != n[gi]]
        print("%-10s %-5s n=%5d differing=%d %s" % (deck, cell, len(common), len(diff), diff[:10]))

print("\n== IDENTITY: every other deck, base binary vs new binary at defaults ==")
bad = 0
tot = 0
for (deck, cell, tag), o in sorted(bw.items()):
    if tag != "id":
        continue
    n = nw.get((deck, cell, "id"), {})
    common = sorted(set(o) & set(n))
    diff = [gi for gi in common if o[gi] != n[gi]]
    tot += len(common)
    if diff or len(common) != len(o):
        bad += 1
        print("  DIFF %-16s %-5s n=%d/%d differing=%s" % (deck, cell, len(common), len(o), diff))
print("identity: %d games compared, %d (deck, cell) with a difference" % (tot, bad))

if slower_out:
    with open(slower_out, "w") as f:
        for s in slower:
            f.write("%s %s %d %d %d\n" % s)
    print("\n%d slower games -> %s" % (len(slower), slower_out))
