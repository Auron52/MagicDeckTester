#!/usr/bin/env python3
"""ONE pooled A/B for MTG_DIG_SEARCHED_CHOICE: arm 'new' (shipped default, lever ON) vs 'dig0' (lever =0),
paired by (deck, cell, game index) on FRESH seeds.

  python3 make_dig_ab.py --out DIR [--decks k1,k2,...] [--seed0 8800001] [--scale 1.0] [--cells ship,d3,d0]
  -> DIR/manifest.json ; run  MTG_DUMP_WINS=1 mtg --batch DIR/manifest.json --threads 6 > DIR/batch.out 2> DIR/batch.err
  python3 make_dig_ab.py --analyse DIR      (reads DIR/batch.err + DIR/batch.out + DIR/manifest.json)

Cells (make_ab5 shape): ship = value_play-driven depth, budget 20, 200 games; d3 b10 300; d0 2000.
Seed of game gi in (deck, cell) = seed0 + 20000 * deck_index + 5000 * cell_index + gi (deck_index = position in
ALL_DECKS, so a subset run keeps the same seeds). Unwon scores 9. Deck files are the PRIMARY checkout's
(byte-identical to the worktree's at 9347f97b modulo CRLF in three .cod files) so the machine-local keep-table
caches, keyed on the sidecar's canonical path, are reused rather than rebuilt.
"""
import argparse, json, math, os, re, sys
from collections import defaultdict

PRIMARY = "/workspaces/MagicDeckTester"
ALL_DECKS = [
    ("fivecolour",   "decks/FiveColour/FiveColour.cod", "decks/FiveColour/FiveColour.profile.json"),
    ("pirates",      "decks/Pirates/Pirates.cod", "decks/Pirates/Pirates.profile.json"),
    ("edf",          "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
                     "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json"),
    ("soldiers",     "decks/Soldiers/soldiers.cod", "decks/Soldiers/soldiers.profile.json"),
    ("whiteknights", "decks/WhiteKnights/WhiteKnights.cod", "decks/WhiteKnights/WhiteKnights.profile.json"),
    ("th",           "decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    ("auras",        "decks/Auras/Auras.cod", "decks/Auras/Auras.profile.json"),
    ("fluctuator",   "decks/Fluctuator/Fluctuator.cod", "decks/Fluctuator/Fluctuator.profile.json"),
    ("dragons",      "decks/Dragons/Dragons.cod", "decks/Dragons/Dragons.profile.json"),
]
KEYS = [d[0] for d in ALL_DECKS]
CELLS = [("ship", None, 20, 200), ("d3", 3, 10, 300), ("d0", 0, 0, 2000)]
CELL_IX = {c[0]: i for i, c in enumerate(CELLS)}
CHUNK = 50
ARMS = [("new", {}), ("dig0", {"MTG_DIG_SEARCHED_CHOICE": False})]
# Per-deck extra arms, compared against 'new' (not part of the lever's per-deck bar): FiveColour is the only
# dig deck with the main-phase filter, so it alone can say whether MTG_DIG_DEFER_MAIN2 still moves play
# once the dig is searched.
EXTRA_ARMS = {"fivecolour": [("defer0", {"MTG_DIG_DEFER_MAIN2": False})]}


def seed_base(seed0, key, cell):
    return seed0 + 20000 * KEYS.index(key) + 5000 * CELL_IX[cell]


def build(a):
    decks = [d for d in ALL_DECKS if not a.decks or d[0] in a.decks.split(",")]
    cells = [c for c in CELLS if c[0] in a.cells.split(",")]
    jobs = []
    for cname, depth, bud, games in cells:          # ship first: the slowest cell's tail overlaps the rest
        for key, deck, prof in decks:
            base = seed_base(a.seed0, key, cname)
            n = max(CHUNK, int(games * a.scale))
            for c in range(0, n, CHUNK):
                for arm, fl in ARMS + EXTRA_ARMS.get(key, []):
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": os.path.join(PRIMARY, deck),
                         "profile": os.path.join(PRIMARY, prof), "games": min(CHUNK, n - c),
                         "seed": base + c, "game_index": c, "budget_ms": bud}
                    if fl:
                        j["flags"] = dict(fl)
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
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
    cost = defaultdict(lambda: [0, 0])
    jpat = re.compile(r"^(\S+?)_(ship|d3|d0)_([a-z0-9]+)\.c\d+: played=\d+ avg=\S+ digest=\S+ ms=(\d+) units=(\d+)")
    if os.path.exists(os.path.join(d, "batch.out")):
        for line in open(os.path.join(d, "batch.out"), errors="ignore"):
            m = jpat.search(line)
            if m:
                c = cost[(m.group(1), m.group(2), m.group(3))]
                c[0] += int(m.group(4))
                c[1] += int(m.group(5))
    print("%-13s %-5s %5s %8s %8s %9s %7s %6s %6s %5s %7s %7s" % ("deck", "cell", "n", "new", "dig0", "new-dig0", "t",
          "faster", "slower", "net", "ms x", "units x"))
    deck_net, slower = defaultdict(int), []
    for (deck, cell) in sorted(res, key=lambda k: (KEYS.index(k[0]) if k[0] in KEYS else 99, CELL_IX[k[1]])):
        r = res[(deck, cell)]
        gis = sorted(set(r.get("new", {})) & set(r.get("dig0", {})))
        if not gis:
            continue
        dd = [r["new"][g] - r["dig0"][g] for g in gis]
        n = len(dd)
        mean = sum(dd) / n
        var = sum((v - mean) ** 2 for v in dd) / max(1, n - 1)
        se = math.sqrt(var / n) if n > 1 else 0.0
        net = sum(dd)
        deck_net[deck] += net
        cn, co = cost[(deck, cell, "new")], cost[(deck, cell, "dig0")]
        print("%-13s %-5s %5d %8.4f %8.4f %+9.4f %7.2f %6d %6d %+5d %7s %7s" % (
            deck, cell, n, sum(r["new"][g] for g in gis) / n, sum(r["dig0"][g] for g in gis) / n, mean,
            mean / se if se > 0 else 0.0, sum(1 for v in dd if v < 0), sum(1 for v in dd if v > 0), net,
            ("%.3f" % (cn[0] / co[0])) if co[0] else "-", ("%.3f" % (cn[1] / co[1])) if co[1] else "-"))
        for g in gis:
            if r["new"][g] > r["dig0"][g]:
                slower.append((deck, cell, g, r["new"][g], r["dig0"][g], seed_base(seed0, deck, cell) + g))
    extra_rows = []
    for (deck, cell) in sorted(res, key=lambda k: (KEYS.index(k[0]) if k[0] in KEYS else 99, CELL_IX[k[1]])):
        r = res[(deck, cell)]
        for x in sorted(k for k in r if k not in ("new", "dig0")):
            gis = sorted(set(r.get("new", {})) & set(r[x]))
            if not gis:
                continue
            dd = [r["new"][g] - r[x][g] for g in gis]
            n = len(dd)
            mean = sum(dd) / n
            var = sum((v - mean) ** 2 for v in dd) / max(1, n - 1)
            se = math.sqrt(var / n) if n > 1 else 0.0
            cn, co = cost[(deck, cell, "new")], cost[(deck, cell, x)]
            extra_rows.append("%-13s %-5s %-7s n=%5d new=%.4f %s=%.4f new-%s=%+.4f t=%.2f faster=%d slower=%d net=%+d ms x %s units x %s"
                              % (deck, cell, x, n, sum(r["new"][g] for g in gis) / n, x, sum(r[x][g] for g in gis) / n,
                                 x, mean, mean / se if se > 0 else 0.0, sum(1 for v in dd if v < 0),
                                 sum(1 for v in dd if v > 0), sum(dd),
                                 ("%.3f" % (cn[0] / co[0])) if co[0] else "-",
                                 ("%.3f" % (cn[1] / co[1])) if co[1] else "-"))
            for g in gis:
                if r["new"][g] != r[x][g]:
                    extra_rows.append("    %s %s gi=%d new=%d %s=%d seed=%d" % (deck, cell, g, r["new"][g], x, r[x][g],
                                                                           seed_base(seed0, deck, cell) + g))
    if extra_rows:
        print("\nEXTRA ARMS (vs new):")
        for line in extra_rows:
            print("  " + line)
    print("\nPER-DECK NET (new - dig0, all cells; bar <= 0):")
    for deck in sorted(deck_net, key=lambda k: KEYS.index(k) if k in KEYS else 99):
        print("  %-13s %+5d%s" % (deck, deck_net[deck], "   <-- ABOVE 0" if deck_net[deck] > 0 else ""))
    print("\nEVERY game where 'new' is SLOWER (deck cell gi new dig0 seed):")
    for s in slower:
        print("  %-13s %-5s gi=%-5d new=%d dig0=%d seed=%d" % s)
    json.dump(slower, open(os.path.join(d, "slower.json"), "w"))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--decks", default="")
    ap.add_argument("--cells", default="ship,d3,d0")
    ap.add_argument("--seed0", type=int, default=8800001)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--analyse")
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
