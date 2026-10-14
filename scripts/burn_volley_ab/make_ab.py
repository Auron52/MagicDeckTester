#!/usr/bin/env python3
"""Burn cleanup-discard: the land-light Shard Volley variants (2026-10-14), ONE pooled paired A/B.

USER 2026-10-14: "that rule is probably too aggressive when we are land-light. At least 1 could always
be kept and 2 possibly. ... It could be second ranking, 'shard volley with no land' that loses to more
of the potential options." Arms are heurarm job flags (src/ai/HeuristicArm.h); every arm of a cell runs
the SAME games (seed = base + gi, game_index = gi, so the opponent spawn schedule matches too).

Cells: ship (no depth key: burn's value_play, d6 b20), d3 b10 (ignore_play_profile), d0 b0.
Seed sets: "old" = the 2026-10-13 discard_amend A/B's seeds (ship 9300001+, d3 9305001+, d0 9310001+),
"new" = fresh held-out seeds (ship 10200001+, d3 10210001+, d0 10220001+).

  python3 make_ab.py --out DIR [--ship-old 2000 --ship-new 2000 --d3-old 4000 --d3-new 2000
                                --d0-old 80000 --d0-new 80000]
  -> MTG_PGO=0 MTG_DUMP_WINS=1 mtg --batch DIR/manifest.json --threads 6 > DIR/batch.out 2> DIR/batch.err
  python3 make_ab.py --analyse DIR      (loss = 9; paired per game)
"""
import argparse, json, os, re, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # the checkout
DECK = "decks/burn/burn.txt"
PROF = "decks/burn/burn.profile.json"

K1, K2, FL = "MTG_BURN_VOLLEY_KEEP1", "MTG_BURN_VOLLEY_KEEP2", "MTG_BURN_VOLLEY_KEEP_FLOOR"
RANK, LATE = "MTG_BURN_VOLLEY_NOLAND_RANK", "MTG_BURN_VOLLEY_NOLAND_LATE"
ARMS = [
    ("generic", {"MTG_BURN_BUCKET_DISCARD": False}),
    ("cur",     {}),
    ("k1",      {K1: True}),
    ("k2",      {K2: True}),
    ("f1",      {K1: True, FL: True}),
    ("f2",      {K2: True, FL: True}),
    ("rank",    {RANK: True}),
    ("late",    {LATE: True}),
    ("k1late",  {K1: True, LATE: True}),
    ("k2late",  {K2: True, LATE: True}),
]
SEEDS = {("ship", "old"): 9300001, ("d3", "old"): 9305001, ("d0", "old"): 9310001,
         ("ship", "new"): 10200001, ("d3", "new"): 10210001, ("d0", "new"): 10220001}
CELL = {"ship": {"budget_ms": 20},
        "d3": {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
        "d0": {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}}
CHUNK = {"ship": 25, "d3": 50, "d0": 500}
COST = {"ship": 100.0, "d3": 10.0, "d0": 1.0}


def build(a):
    sizes = {("ship", "old"): a.ship_old, ("ship", "new"): a.ship_new, ("d3", "old"): a.d3_old,
             ("d3", "new"): a.d3_new, ("d0", "old"): a.d0_old, ("d0", "new"): a.d0_new}
    jobs = []
    for (cell, sset), n in sizes.items():
        ch = CHUNK[cell]
        for c in range(0, n, ch):
            for arm, fl in ARMS:   # interleaved: every arm's chunk c back to back
                if a.arms and arm not in a.arms.split(","):
                    continue
                j = {"name": "burn_%s_%s_%s.c%d" % (cell, sset, arm, c),
                     "deck": os.path.join(ROOT, DECK), "profile": os.path.join(ROOT, PROF),
                     "games": min(ch, n - c), "seed": SEEDS[(cell, sset)] + c, "game_index": c}
                j.update(CELL[cell])
                if fl:
                    j["flags"] = dict(fl)
                jobs.append(j)
    jobs.sort(key=lambda j: -COST[j["name"].split("_")[1]])   # stable: expensive cells first
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump({"arms": ARMS, "seeds": {"%s_%s" % k: v for k, v in SEEDS.items()},
               "sizes": {"%s_%s" % k: v for k, v in sizes.items()}},
              open(os.path.join(a.out, "design.json"), "w"), indent=1)
    print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))


def load(d):
    pat = re.compile(r"\[win\] job=burn_(ship|d3|d0)_(old|new)_([a-z0-9]+)\.c\d+ gi=(\d+) wt=(-?\d+)")
    wins = defaultdict(dict)
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(5))
            wins[(m.group(1), m.group(2), m.group(3))][int(m.group(4))] = 9 if wt < 0 else wt
    return wins


def compare(wa, wb):
    gis = sorted(set(wa) & set(wb))
    dd = {g: wa[g] - wb[g] for g in gis}
    fast = [g for g in gis if dd[g] < 0]
    slow = [g for g in gis if dd[g] > 0]
    return len(gis), fast, slow, sum(dd.values()), (sum(wa[g] for g in gis) / len(gis) if gis else 0.0)


def analyse(d):
    wins = load(d)
    design = json.load(open(os.path.join(d, "design.json")))
    seeds = design["seeds"]
    arms = [a for a, _ in ARMS]
    out = []
    pr = out.append
    pr("loss = 9; paired per game; faster/slower/net = arm - base (negative net = arm faster)")
    for base in ("cur", "generic"):
        pr("\n== vs %s ==" % base)
        pr("%-5s %-4s %-8s %6s %8s %8s %6s %6s %6s" % ("cell", "set", "arm", "n", "mean", "base", "fast",
                                                    "slow", "net"))
        for cell in ("ship", "d3", "d0"):
            for sset in ("old", "new", "all"):
                for arm in arms:
                    if arm == base:
                        continue
                    if sset == "all":
                        wa = {("o", g): v for g, v in wins.get((cell, "old", arm), {}).items()}
                        wa.update({("n", g): v for g, v in wins.get((cell, "new", arm), {}).items()})
                        wb = {("o", g): v for g, v in wins.get((cell, "old", base), {}).items()}
                        wb.update({("n", g): v for g, v in wins.get((cell, "new", base), {}).items()})
                    else:
                        wa, wb = wins.get((cell, sset, arm), {}), wins.get((cell, sset, base), {})
                    n, fast, slow, net, ma = compare(wa, wb)
                    if n == 0:
                        continue
                    mb = sum(wb[g] for g in wa if g in wb) / n
                    pr("%-5s %-4s %-8s %6d %8.4f %8.4f %6d %6d %+6d" % (cell, sset, arm, n, ma, mb, len(fast),
                                                                     len(slow), net))
    pr("\n== every game where any arm differs from cur (cell set gi seed: per-arm win turn) ==")
    for cell in ("ship", "d3", "d0"):
        for sset in ("old", "new"):
            base = wins.get((cell, sset, "cur"), {})
            moved = set()
            for arm in arms:
                w = wins.get((cell, sset, arm), {})
                moved |= {g for g in w if g in base and w[g] != base[g]}
            for g in sorted(moved):
                row = " ".join("%s=%s" % (arm, wins.get((cell, sset, arm), {}).get(g, "?")) for arm in arms)
                pr("  %-4s %-3s gi=%-6d seed=%d  %s" % (cell, sset, g, seeds["%s_%s" % (cell, sset)] + g, row))
    named = [1048, 8292, 12620, 17376, 20102]
    pr("\n== the five named d0 games (old seeds 9310001 + gi) ==")
    for g in named:
        row = " ".join("%s=%s" % (arm, wins.get(("d0", "old", arm), {}).get(g, "?")) for arm in arms)
        pr("  gi=%-6d seed=%d  %s" % (g, SEEDS[("d0", "old")] + g, row))
    text = "\n".join(out)
    print(text)
    open(os.path.join(d, "analysis.txt"), "w").write(text + "\n")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--analyse")
    ap.add_argument("--arms", default="", help="comma list: only these arms (default all)")
    ap.add_argument("--ship-old", type=int, default=2000)
    ap.add_argument("--ship-new", type=int, default=2000)
    ap.add_argument("--d3-old", type=int, default=4000)
    ap.add_argument("--d3-new", type=int, default=2000)
    ap.add_argument("--d0-old", type=int, default=80000)
    ap.add_argument("--d0-new", type=int, default=80000)
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
