#!/usr/bin/env python3
"""Does the searched dig's budget churn shrink as the budget grows? (the completeness invariant: a lever that
re-prices work must converge as the budget rises). Arms 'new' (MTG_DIG_SEARCHED_CHOICE on) vs 'dig0' (=0),
paired by game index, one deck at --depth D over several budgets, FRESH seeds.

  python3 make_budget_map.py --out DIR [--deck th] [--depth 3] [--budgets 20,40,80] [--games 1200] [--seed0 9600001]
  -> DIR/manifest.json ; run with MTG_DUMP_WINS=1 ... --batch DIR/manifest.json
  python3 make_budget_map.py --analyse DIR
Seed of game gi at budget index b: seed0 + 5000 * b + gi (the same deals at every budget). Unwon scores 9.
"""
import argparse, json, math, os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_dig_ab import ALL_DECKS, CHUNK, PRIMARY  # noqa: E402

ARMS = [("dig0", {"MTG_DIG_SEARCHED_CHOICE": False}), ("new", {})]


def build(a):
    key, deck, prof = [d for d in ALL_DECKS if d[0] == a.deck][0]
    jobs = []
    budgets = [int(b) for b in a.budgets.split(",")]
    for bi, bud in sorted(enumerate(budgets), key=lambda t: -t[1]):   # biggest budget first: the slow tail
        for c in range(0, a.games, CHUNK):
            for arm, fl in ARMS:
                j = {"name": "%s_b%d_%s.c%d" % (key, bud, arm, c), "deck": os.path.join(PRIMARY, deck),
                     "profile": os.path.join(PRIMARY, prof), "games": min(CHUNK, a.games - c),
                     "seed": a.seed0 + 5000 * bi + c, "game_index": c, "budget_ms": bud, "depth": a.depth,
                     "ignore_play_profile": True}
                if fl:
                    j["flags"] = dict(fl)
                jobs.append(j)
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump({"seed0": a.seed0, "budgets": budgets}, open(os.path.join(a.out, "seeds.json"), "w"))
    print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))


def analyse(d):
    meta = json.load(open(os.path.join(d, "seeds.json")))
    pat = re.compile(r"\[win\] job=(\S+?)_b(\d+)_([a-z0-9]+)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(lambda: defaultdict(dict))
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(6))
            res[int(m.group(2))][m.group(3)][int(m.group(5))] = 9 if wt < 0 else wt
    units = defaultdict(int)
    jpat = re.compile(r"^(\S+?)_b(\d+)_([a-z0-9]+)\.c\d+: played=\d+ avg=\S+ digest=\S+ ms=\d+ units=(\d+)")
    for line in open(os.path.join(d, "batch.out"), errors="ignore"):
        m = jpat.search(line)
        if m:
            units[(int(m.group(2)), m.group(3))] += int(m.group(4))
    print("%6s %5s %8s %8s %9s %6s %6s %6s %5s %8s" % ("budget", "n", "new", "dig0", "diff", "t", "faster",
                                                       "slower", "net", "units x"))
    slower = []
    for bud in sorted(res):
        r = res[bud]
        gis = sorted(set(r.get("new", {})) & set(r.get("dig0", {})))
        if not gis:
            continue
        dd = [r["new"][g] - r["dig0"][g] for g in gis]
        n = len(dd)
        mean = sum(dd) / n
        var = sum((v - mean) ** 2 for v in dd) / max(1, n - 1)
        se = math.sqrt(var / n) if n > 1 else 0.0
        bi = meta["budgets"].index(bud)
        for g in gis:
            if r["new"][g] > r["dig0"][g]:
                slower.append((bud, g, r["new"][g], r["dig0"][g], meta["seed0"] + 5000 * bi + g))
        u0 = units[(bud, "dig0")]
        print("%6d %5d %8.4f %8.4f %+9.4f %6.2f %6d %6d %+5d %8s" % (
            bud, n, sum(r["new"][g] for g in gis) / n, sum(r["dig0"][g] for g in gis) / n, mean,
            mean / se if se > 0 else 0.0, sum(1 for v in dd if v < 0), sum(1 for v in dd if v > 0), sum(dd),
            ("%.3f" % (units[(bud, "new")] / u0)) if u0 else "-"))
    print("slower (budget gi new dig0 seed):")
    for s in slower:
        print("  b%-4d gi=%-5d new=%d dig0=%d seed=%d" % s)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--deck", default="th")
    ap.add_argument("--depth", type=int, default=3)
    ap.add_argument("--budgets", default="20,40,80")
    ap.add_argument("--games", type=int, default=1200)
    ap.add_argument("--seed0", type=int, default=9600001)
    ap.add_argument("--analyse")
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
