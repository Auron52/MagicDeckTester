#!/usr/bin/env python3
"""Paired analysis of the v6 A/B. usage: analyze_v6.py <v6_dir> [<v4_dir>] [--slower-out PREFIX]

<v6_dir>/wins holds <deck>.<cell>.<arm>.c<off>.wins (+ .units). Loss = 9.
StompySurprise (stompy, stompy2hg): every arm vs off, and the increments (ofixall vs ofix, ofixall vs
ofixsvnpd, onotopall vs onotop, ofixall vs onotopall).
With <v4_dir> (its wins_new): the identity check (v6 off / ofix / onotop vs v4's same arms, game for game),
and the other suite decks' screen -- v6 fixall vs v4 off and vs v4 fix on the same games.
--slower-out PREFIX writes PREFIX.<arm>.txt (deck cell gi old new): every slower game vs off (StompySurprise
arms, and fixall on the other decks against v4's off).
"""
import collections
import glob
import os
import sys

LOSS = 9
args = list(sys.argv[1:])
pre = None
if "--slower-out" in args:
    i = args.index("--slower-out")
    pre = args[i + 1]
    del args[i:i + 2]
d6 = args[0]
d4 = args[1] if len(args) > 1 else None


def load(pattern):
    wins = collections.defaultdict(dict)
    units = collections.defaultdict(dict)
    for p in glob.glob(pattern):
        parts = os.path.basename(p)[:-5].split(".")
        if len(parts) != 4:
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
    return wins, units


wins, units = load(os.path.join(d6, "wins", "*.wins"))
w4, u4 = load(os.path.join(d4, "wins_new", "*.wins")) if d4 else ({}, {})


def compare(wa, wb, ua, ub):
    common = sorted(set(wa) & set(wb))
    if not common:
        return None
    fast = slow = net = ident = 0
    slowg = []
    for gi in common:
        (xa, da), (xb, db) = wa[gi], wb[gi]
        if xa < xb:
            fast += 1
        elif xa > xb:
            slow += 1
            slowg.append((gi, xb, xa))
        net += xa - xb
        if xa == xb and da == db:
            ident += 1
    tua = sum(ua.get(g, 0) for g in common)
    tub = sum(ub.get(g, 0) for g in common)
    return dict(n=len(common), a=sum(wa[g][0] for g in common) / len(common),
                b=sum(wb[g][0] for g in common) / len(common), fast=fast, slow=slow, net=net, ident=ident,
                ur=(tua / tub) if tub else None, slowg=slowg)


HDR = "%-16s %-5s %-22s %6s %8s %8s %5s %5s %6s %6s %8s"


def row(deck, cell, label, r):
    print(HDR % (deck, cell, label, r["n"], "%.4f" % r["b"], "%.4f" % r["a"], r["fast"], r["slow"],
                 "%+d" % r["net"], r["ident"], ("%.3fx" % r["ur"]) if r["ur"] else "-"))


ARMS = ("ofix", "onotop", "ofixall", "onotopall", "ofixsvnpd", "fixall")
INCR = (("ofixall", "ofix"), ("ofixall", "ofixsvnpd"), ("onotopall", "onotop"), ("ofixall", "onotopall"))
slower = collections.defaultdict(list)
print("loss = %d; paired per game; net = sum(arm - ref)\n" % LOSS)
print(HDR % ("deck", "cell", "arm vs ref", "n", "ref", "arm", "fast", "slow", "net", "ident", "units"))
for deck in ("stompy", "stompy2hg"):
    for cell in ("ship", "d3", "d0"):
        for a, b in [(x, "off") for x in ARMS] + list(INCR):
            ka, kb = (deck, cell, a), (deck, cell, b)
            r = compare(wins.get(ka, {}), wins.get(kb, {}), units.get(ka, {}), units.get(kb, {}))
            if r is None:
                continue
            row(deck, cell, "%s vs %s" % (a, b), r)
            if b == "off":
                for gi, wo, wn in r["slowg"]:
                    slower[a].append((deck, cell, gi, wo, wn))
        print()
if d4:
    print("identity vs v4 (svn / pd / pf default OFF): deck cell arm, games compared, differing")
    for deck in ("stompy", "stompy2hg"):
        for cell in ("ship", "d3", "d0"):
            for arm in ("off", "ofix", "onotop"):
                a, b = wins.get((deck, cell, arm), {}), w4.get((deck, cell, arm), {})
                common = sorted(set(a) & set(b))
                diff = [g for g in common if a[g] != b[g]]
                if common:
                    print("  %-10s %-5s %-7s %6d %4d %s" % (deck, cell, arm, len(common), len(diff), diff[:8]))
    print("\nother suite decks: v6 fixall vs v4 off, and vs v4 fix (same games)")
    print(HDR % ("deck", "cell", "arm vs ref", "n", "ref", "arm", "fast", "slow", "net", "ident", "units"))
    others = sorted(set(k[0] for k in wins if k[0] not in ("stompy", "stompy2hg")))
    for deck in others:
        for cell in ("ship", "d3", "d0"):
            k = (deck, cell, "fixall")
            for ref in ("off", "fix"):
                r = compare(wins.get(k, {}), w4.get((deck, cell, ref), {}), units.get(k, {}),
                            u4.get((deck, cell, ref), {}))
                if r is None:
                    continue
                if ref == "fix" and r["ident"] == r["n"]:
                    continue   # fixall plays exactly as fix: pd / pf inert here
                row(deck, cell, "fixall vs v4 %s" % ref, r)
                if ref == "off":
                    for gi, wo, wn in r["slowg"]:
                        slower["fixall"].append((deck, cell, gi, wo, wn))
if pre:
    for arm in ARMS:
        with open("%s.%s.txt" % (pre, arm), "w") as fh:
            for s in slower[arm]:
                fh.write("%s %s %d %d %d\n" % s)
    print("\nslower games per arm vs off: " + ", ".join("%s %d" % (a, len(slower[a])) for a in ARMS))
