#!/usr/bin/env python3
"""ONE pooled A/B for MTG_DIG_TWIN_LAST (the node-local "twins last" move order; generic-dig-gate.md, "A later
wave for the twins", option 1). Three arms, paired by (deck, cell, game index) on FRESH seeds:

  dig0  MTG_DIG_SEARCHED_CHOICE=0          == the committed tree (byte-identical; the per-deck bar's baseline)
  new   the lever as shipped on the branch  (MTG_DIG_SEARCHED_CHOICE on, twins interleaved)
  tl    new + MTG_DIG_TWIN_LAST=1

  python3 make_twinlast_ab.py --out DIR [--decks k1,...] [--seed0 9300001] [--cells ship,d3,d0] [--focus th,fivecolour]
                              [--focus-scale 4]
  -> DIR/manifest.json ; run  bash run_batch.sh <mtg> DIR   (or any --batch runner with MTG_DUMP_WINS=1)
  python3 make_twinlast_ab.py --analyse DIR

Cells as make_dig_ab.py (ship = value_play depth b20 200 games; d3 b10 300; d0 2000). The --focus decks get
--focus-scale x the d3 cell (the step-5 larger held-out sample for the two decks that missed the bar). Seeds:
seed0 + 20000 * deck_index + 5000 * cell_index + gi (deck_index = position in ALL_DECKS). Unwon scores 9.
"""
import argparse, json, math, os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_dig_ab import ALL_DECKS, KEYS, CELLS, CELL_IX, CHUNK, PRIMARY  # noqa: E402

ARMS = [("dig0", {"MTG_DIG_SEARCHED_CHOICE": False}), ("new", {}), ("tl", {"MTG_DIG_TWIN_LAST": True})]
PAIRS = [("tl", "dig0"), ("new", "dig0"), ("tl", "new")]


def seed_base(seed0, key, cell):
    return seed0 + 20000 * KEYS.index(key) + 5000 * CELL_IX[cell]


def build(a):
    decks = [d for d in ALL_DECKS if not a.decks or d[0] in a.decks.split(",")]
    cells = [c for c in CELLS if c[0] in a.cells.split(",")]
    focus = set(a.focus.split(",")) if a.focus else set()
    jobs = []
    for cname, depth, bud, games in cells:          # ship first: the slowest cell's tail overlaps the rest
        for key, deck, prof in decks:
            base = seed_base(a.seed0, key, cname)
            n = games * (a.focus_scale if (key in focus and cname == "d3") else 1)
            for c in range(0, n, CHUNK):
                for arm, fl in ARMS:
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": os.path.join(PRIMARY, deck),
                         "profile": os.path.join(PRIMARY, prof), "games": min(CHUNK, n - c),
                         "seed": base + c, "game_index": c, "budget_ms": bud}
                    if fl:
                        j["flags"] = dict(fl)
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
                    jobs.append(j)
    # EXACTNESS (complete at every budget): d3 at --budget-ms 0, arms new / tl -- a pure reorder must give
    # every game the same win turn (only a tied line may differ). Seeds seed0 + 190000 + 1000 * deck_index.
    if a.exact:
        for key, deck, prof in decks:
            for arm, fl in ARMS[1:]:
                j = {"name": "%s_x0_%s.c0" % (key, arm), "deck": os.path.join(PRIMARY, deck),
                     "profile": os.path.join(PRIMARY, prof), "games": 10 if key == "edf" else a.exact,
                     "seed": a.seed0 + 190000 + 1000 * KEYS.index(key), "game_index": 0, "budget_ms": 0,
                     "depth": 3, "ignore_play_profile": True}
                if fl:
                    j["flags"] = dict(fl)
                jobs.append(j)
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump({"seed0": a.seed0}, open(os.path.join(a.out, "seeds.json"), "w"))
    print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))


def analyse(d):
    seed0 = json.load(open(os.path.join(d, "seeds.json")))["seed0"]
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3|d0)_([a-z0-9]+)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(lambda: defaultdict(dict))
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(6))
            res[(m.group(1), m.group(2))][m.group(3)][int(m.group(5))] = 9 if wt < 0 else wt
    cost, digests = defaultdict(lambda: [0, 0]), defaultdict(dict)
    jpat = re.compile(r"^(\S+?)_(ship|d3|d0)_([a-z0-9]+)\.c(\d+): played=\d+ avg=\S+ digest=(\S+) ms=(\d+) units=(\d+)")
    if os.path.exists(os.path.join(d, "batch.out")):
        for line in open(os.path.join(d, "batch.out"), errors="ignore"):
            m = jpat.search(line)
            if m:
                c = cost[(m.group(1), m.group(2), m.group(3))]
                c[0] += int(m.group(6))
                c[1] += int(m.group(7))
                digests[(m.group(1), m.group(2), int(m.group(4)))][m.group(3)] = m.group(5)
    xpat = re.compile(r"\[win\] job=(\S+?)_x0_([a-z0-9]+)\.c0 gi=(\d+) wt=(-?\d+)")
    ex = defaultdict(dict)
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = xpat.search(line)
        if m:
            ex[(m.group(1), int(m.group(3)))][m.group(2)] = int(m.group(4))
    if ex:
        pairs = [v for v in ex.values() if "new" in v and "tl" in v]
        bad = sorted(k for k, v in ex.items() if "new" in v and "tl" in v and v["new"] != v["tl"])
        print("EXACTNESS d3 b0 (new vs tl win turn per game): %d games, %d differ%s" % (
            len(pairs), len(bad), "" if not bad else ": " + ", ".join("%s gi%d %d/%d" % (k[0], k[1],
            ex[k]["new"], ex[k]["tl"]) for k in bad)))
    order = lambda k: (KEYS.index(k[0]) if k[0] in KEYS else 99, CELL_IX[k[1]])
    slower = defaultdict(list)
    for x, y in PAIRS:
        print("\n==== %s vs %s ====" % (x, y))
        print("%-13s %-5s %5s %8s %8s %9s %7s %6s %6s %5s %7s %7s %9s" % ("deck", "cell", "n", x, y, "diff", "t",
              "faster", "slower", "net", "ms x", "units x", "chunks~"))
        deck_net = defaultdict(int)
        for (deck, cell) in sorted(res, key=order):
            r = res[(deck, cell)]
            gis = sorted(set(r.get(x, {})) & set(r.get(y, {})))
            if not gis:
                continue
            dd = [r[x][g] - r[y][g] for g in gis]
            n = len(dd)
            mean = sum(dd) / n
            var = sum((v - mean) ** 2 for v in dd) / max(1, n - 1)
            se = math.sqrt(var / n) if n > 1 else 0.0
            net = sum(dd)
            deck_net[deck] += net
            cx, cy = cost[(deck, cell, x)], cost[(deck, cell, y)]
            ch = [k for k in digests if k[0] == deck and k[1] == cell and x in digests[k] and y in digests[k]]
            moved = sum(1 for k in ch if digests[k][x] != digests[k][y])
            print("%-13s %-5s %5d %8.4f %8.4f %+9.4f %7.2f %6d %6d %+5d %7s %7s %9s" % (
                deck, cell, n, sum(r[x][g] for g in gis) / n, sum(r[y][g] for g in gis) / n, mean,
                mean / se if se > 0 else 0.0, sum(1 for v in dd if v < 0), sum(1 for v in dd if v > 0), net,
                ("%.3f" % (cx[0] / cy[0])) if cy[0] else "-", ("%.3f" % (cx[1] / cy[1])) if cy[1] else "-",
                "%d/%d" % (moved, len(ch))))
            for g in gis:
                if r[x][g] > r[y][g]:
                    slower[(x, y)].append((deck, cell, g, r[x][g], r[y][g], seed_base(seed0, deck, cell) + g))
        print("PER-DECK NET (%s - %s, all cells; bar <= 0):" % (x, y))
        for deck in sorted(deck_net, key=lambda k: KEYS.index(k) if k in KEYS else 99):
            print("  %-13s %+5d%s" % (deck, deck_net[deck], "   <-- ABOVE 0" if deck_net[deck] > 0 else ""))
        print("EVERY game where %s is SLOWER than %s (deck cell gi %s %s seed):" % (x, y, x, y))
        for s in slower[(x, y)]:
            print("  %-13s %-5s gi=%-5d %s=%d %s=%d seed=%d" % (s[0], s[1], s[2], x, s[3], y, s[4], s[5]))
    json.dump({"%s-%s" % k: v for k, v in slower.items()}, open(os.path.join(d, "slower.json"), "w"))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--decks", default="")
    ap.add_argument("--cells", default="ship,d3,d0")
    ap.add_argument("--seed0", type=int, default=9300001)
    ap.add_argument("--focus", default="th,fivecolour")
    ap.add_argument("--focus-scale", type=int, default=4)
    ap.add_argument("--exact", type=int, default=30, help="games per deck in the d3 b0 exactness cell (0 = none)")
    ap.add_argument("--analyse")
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
