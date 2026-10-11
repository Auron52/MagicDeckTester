#!/usr/bin/env python3
"""ONE pooled batch: the three proposed discard amendments, each against the shipped policy, paired.

    python3 logs/discard_impl/make_ab2.py            -> logs/discard_impl/ab2/manifest.json
    MTG_DUMP_WINS=1 build/PGO/mtg --batch logs/discard_impl/ab2/manifest.json --threads N 2> .../batch.err
    python3 logs/discard_impl/make_ab2.py --analyse logs/discard_impl/ab2/batch.err

Arms: 'on' = the shipped default (the USER's design as built), 'amend' = on + the amendment lever.
Seeds are DISJOINT per (deck, cell) and from ab1 (a game's deal = job seed + local game number).
"""
import argparse, json, math, os, re, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

REPO = make_ab.REPO
DECKS = {   # suite key -> (policy flag, amendment lever)
    "giants":   ("MTG_GIANTS_BUCKET_DISCARD", "MTG_GIANTS_ROCK_LIBRARY_LAND"),
    "edf":      ("MTG_EDF_BUCKET_DISCARD", "MTG_EDF_OUTLET_PIPS"),
    "selesnya": ("MTG_SELESNYA_BUCKET_DISCARD", "MTG_SELESNYA_ELF_WINDOW"),
}
CELLS = [("ship", None, 20, 600), ("d3", 3, 10, 1000), ("d0", 0, 0, 5000)]
CHUNK = 50
SEED0 = 400001


def build(out):
    maps = make_ab.suite_maps()
    jobs = []
    for i, (key, (flag, lever)) in enumerate(DECKS.items()):
        deck, prof = make_ab.EXTRA_FILES.get(key) or (maps["DECK_FILE"][key], maps["DECK_PROF"][key])
        for ci, (cname, depth, bud, games) in enumerate(CELLS):
            base = SEED0 + 20000 * i + 5000 * ci
            for c in range(0, games, CHUNK):
                for arm, fl in (("on", {flag: True}), ("amend", {flag: True, lever: True})):
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": deck, "profile": prof,
                         "games": CHUNK, "seed": base + c, "game_index": c, "budget_ms": bud, "flags": fl}
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
                    jobs.append(j)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "manifest.json")
    json.dump({"jobs": jobs}, open(path, "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (path, len(jobs), sum(j["games"] for j in jobs)))


def analyse(err_path):
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3|d0)_(on|amend)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(dict)
    for line in open(err_path, errors="ignore"):
        m = pat.search(line)
        if m:
            deck, cell, arm, c, gi, wt = m.group(1), m.group(2), m.group(3), int(m.group(4)), int(m.group(5)), int(m.group(6))
            res[(deck, cell)].setdefault(arm, {})[gi] = (9 if wt < 0 else wt, c)
    print("%-10s %-5s %5s %8s %10s %7s %6s %6s" % ("deck", "cell", "n", "on", "amend-on", "t", "faster", "slower"))
    changed = []
    for (deck, cell) in sorted(res):
        a = res[(deck, cell)]
        gis = sorted(set(a.get("on", {})) & set(a.get("amend", {})))
        d = [a["amend"][g][0] - a["on"][g][0] for g in gis]
        n = len(d)
        if n == 0:
            continue
        mean = sum(d) / n
        var = sum((x - mean) ** 2 for x in d) / max(1, n - 1)
        se = math.sqrt(var / n) if n > 1 else 0.0
        print("%-10s %-5s %5d %8.4f %+10.4f %7.2f %6d %6d" % (deck, cell, n, sum(a["on"][g][0] for g in gis) / n,
              mean, mean / se if se > 0 else 0.0, sum(1 for x in d if x < 0), sum(1 for x in d if x > 0)))
        ci = [c[0] for c in CELLS].index(cell)
        base = SEED0 + 20000 * list(DECKS).index(deck) + 5000 * ci
        for g in gis:
            if a["amend"][g][0] != a["on"][g][0]:
                changed.append((deck, cell, g, a["on"][g][0], a["amend"][g][0], base + g))
    print("\nchanged games (deck cell gi on amend seed):")
    for row in changed:
        print("  %-9s %-4s gi=%-5d on=%d amend=%d %s seed=%d" % (row[0], row[1], row[2], row[3], row[4],
              "SLOWER" if row[4] > row[3] else "faster", row[5]))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "logs/discard_impl/ab2"))
    ap.add_argument("--analyse", default=None)
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a.out)
