#!/usr/bin/env python3
"""ONE pooled batch: the USER's 2026-10-10 follow-up rulings, paired, three decks.

    python3 logs/discard_impl/make_ab4.py                     -> logs/discard_impl/ab4/manifest.json
    MTG_DUMP_WINS=1 <bin> --batch logs/discard_impl/ab4/manifest.json --threads N 2> .../batch.err > .../batch.out
    python3 logs/discard_impl/make_ab4.py --analyse logs/discard_impl/ab4/batch.err

Arms per deck ('new' = the tip's defaults, every other arm = new with one lever flipped):
  giants      old = MTG_GIANTS_ROCK_AFTER_LANDS=0   (the dead-rock rule as first built)
  antilife    branch = MTG_ANTILIFE_TUTOR_BRANCH=1  (the proof arm: the tutor axis scores enabler AND payoff)
  dragonstorm old = MTG_DRAGONSTORM_USER_ORDER=0    (the generic ranks; the USER's order off)
The analysis pairs every arm against 'new' and lists every game where the arm differs (both directions),
with its seed: the narrowing / the new rule must not lose a game the other arm wins without a root cause.
Seeds: SEED0 = 3000001, disjoint from ab1 (<1e6), ab2 and ab3 (1000001 + 20000*i + 5000*ci, i < 34).
"""
import argparse, json, math, os, re, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

REPO = make_ab.REPO
DECKS = {   # key -> (arms besides 'new', cells (name, depth, budget_ms, games))
    "giants":      ([("old", {"MTG_GIANTS_ROCK_AFTER_LANDS": False})],
                    [("ship", None, 20, 400), ("d3", 3, 10, 600), ("d0", 0, 0, 3000)]),
    "antilife":    ([("branch", {"MTG_ANTILIFE_TUTOR_BRANCH": True})],
                    [("ship", None, 20, 600), ("d3", 3, 10, 600), ("2hg", 3, 10, 300)]),
    "dragonstorm": ([("old", {"MTG_DRAGONSTORM_USER_ORDER": False})],
                    [("ship", None, 20, 600), ("d3", 3, 10, 600), ("d0", 0, 0, 2000), ("2hg", 3, 10, 300)]),
}
CELL_IX = {"ship": 0, "d3": 1, "d0": 2, "2hg": 3}
CHUNK = 50
SEED0 = 3000001


def seed_base(key, cell):
    return SEED0 + 20000 * sorted(DECKS).index(key) + 5000 * CELL_IX[cell]


def build(out, only):
    maps = make_ab.suite_maps()
    jobs = []
    for key in sorted(DECKS):
        if only and key not in only:
            continue
        deck, prof = make_ab.EXTRA_FILES.get(key) or (maps["DECK_FILE"][key], maps["DECK_PROF"][key])
        arms, cells = DECKS[key]
        for cname, depth, bud, games in cells:
            base = seed_base(key, cname)
            for c in range(0, games, CHUNK):
                for arm, fl in [("new", {})] + arms:
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": deck, "profile": prof,
                         "games": CHUNK, "seed": base + c, "game_index": c, "budget_ms": bud}
                    if fl:
                        j["flags"] = dict(fl)
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
                    if cname == "2hg":
                        j["starting_life"] = 30
                        j["opponent_heads"] = 2
                    jobs.append(j)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "manifest.json")
    json.dump({"jobs": jobs}, open(path, "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (path, len(jobs), sum(j["games"] for j in jobs)))


def analyse(err_path):
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3|d0|2hg)_([a-z0-9_]+)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(lambda: defaultdict(dict))
    for line in open(err_path, errors="ignore"):
        m = pat.search(line)
        if m:
            res[(m.group(1), m.group(2))][m.group(3)][int(m.group(5))] = 9 if int(m.group(6)) < 0 else int(m.group(6))
    print("%-24s %-5s %5s %8s %9s %7s %6s %6s" % ("deck[arm-new]", "cell", "n", "new", "arm-new", "t", "armfast", "armslow"))
    diffs = []
    for (deck, cell) in sorted(res):
        a = res[(deck, cell)]
        for x in sorted(k for k in a if k != "new"):
            gis = sorted(set(a[x]) & set(a["new"]))
            d = [a[x][g] - a["new"][g] for g in gis]
            n = len(d)
            if not n:
                continue
            mean = sum(d) / n
            var = sum((v - mean) ** 2 for v in d) / max(1, n - 1)
            se = math.sqrt(var / n) if n > 1 else 0.0
            f, s_ = sum(1 for v in d if v < 0), sum(1 for v in d if v > 0)
            print("%-24s %-5s %5d %8.4f %+9.4f %7.2f %6d %6d" % ("%s[%s]" % (deck, x), cell, n,
                  sum(a["new"][g] for g in gis) / n, mean, mean / se if se > 0 else 0.0, f, s_))
            for g in gis:
                if a[x][g] != a["new"][g]:
                    diffs.append((deck, cell, x, g, a["new"][g], a[x][g], seed_base(deck, cell) + g))
    print("\nEVERY differing game (deck cell arm gi new arm seed) -- 'new' slower when new > arm:")
    for r in diffs:
        print("  %-12s %-4s %-7s gi=%-5d new=%d %s=%d seed=%d%s" % (r[0], r[1], r[2], r[3], r[4], r[2], r[5], r[6],
              "   <-- new SLOWER" if r[4] > r[5] else ""))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "logs/discard_impl/ab4"))
    ap.add_argument("--only", default="", help="comma-separated deck keys (default: all three)")
    ap.add_argument("--analyse", default=None)
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a.out, set(filter(None, a.only.split(","))))
