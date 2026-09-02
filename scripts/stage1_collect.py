#!/usr/bin/env python3
"""Collect scripts/stage1_census.sh output into one table.

The headline number is `acted` summed over the IN-TREE sites 0-8 -- greedy Solve calls that
DECIDED something. Site 90 (the horizon leaf) is reported separately and is explicitly OUT OF
SCOPE (USER 2026-09-02: "I'm not worried about the leaf, but I am concerned about greedy within
the search window"). Volume (`g[]`) is not harm; `acted` is.

The `why` breakdown per site is the inventory: masked / base / nested / overrun.
"""
import re, sys, pathlib, collections

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "logs/stage1/census")
ARMS = ["base", "node", "d56", "node0", "d560"]
DECKS = ["mirrorwing", "kitty", "hinata", "th", "auras", "burn",
         "dragonstorm", "antilife", "creature_giving"]

SITE = re.compile(r"s(\d+)=(\d+)\(acted (\d+)\)")
WHY = re.compile(r"^\s+s(\d+) unresolved=(\d+):\s+(.*)$")
WHYPART = re.compile(r"\b(masked|base|nested|overrun|nohost)=(\d+)")
AVG = re.compile(r"[Aa]verage[^0-9]*([0-9]+\.[0-9]+)")


def parse(deck, arm):
    err = OUT / f"{deck}.{arm}.err"
    out = OUT / f"{deck}.{arm}.out"
    if not err.exists():
        return None
    txt = err.read_text(errors="replace")
    line = next((l for l in txt.splitlines() if "GREEDY SITES" in l), "")
    sites = {int(s): (int(g), int(a)) for s, g, a in SITE.findall(line)}
    why = {}
    for l in txt.splitlines():
        m = WHY.match(l)
        if m:
            why[int(m.group(1))] = dict((k, int(v)) for k, v in WHYPART.findall(m.group(3)))
    avg = ""
    if out.exists():
        hits = AVG.findall(out.read_text(errors="replace"))
        avg = hits[-1] if hits else ""
    intree = sum(a for s, (g, a) in sites.items() if s <= 8)
    leaf = sum(a for s, (g, a) in sites.items() if s == 90)
    return dict(sites=sites, why=why, avg=avg, intree=intree, leaf=leaf)


rows = {}
for d in DECKS:
    for a in ARMS:
        r = parse(d, a)
        if r:
            rows[(d, a)] = r

print(f"{'deck':<16}" + "".join(f"{a:>14}" for a in ARMS) + "   (acted, in-tree sites 0-8)")
for d in DECKS:
    cells = []
    for a in ARMS:
        r = rows.get((d, a))
        cells.append(f"{r['intree']:>14,}" if r else f"{'-':>14}")
    print(f"{d:<16}" + "".join(cells))

print()
print("WHY the fallback happened, per site (share of unresolved):")
for d in DECKS:
    if not any((d, a) in rows for a in ARMS):
        continue
    interesting = any(rows[(d, a)]["why"] for a in ARMS if (d, a) in rows)
    if not interesting:
        continue
    print(f"  {d}")
    for a in ARMS:
        r = rows.get((d, a))
        if not r:
            continue
        if not r["why"]:
            print(f"    {a:<6} (no in-tree greedy)")
            continue
        for s, parts in sorted(r["why"].items()):
            tot = sum(parts.values()) or 1
            frag = "  ".join(f"{k}={v/tot*100:.1f}%" for k, v in parts.items())
            print(f"    {a:<6} s{s} unresolved={tot:>10,}  {frag}")
