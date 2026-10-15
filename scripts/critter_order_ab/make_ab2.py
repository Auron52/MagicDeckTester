#!/usr/bin/env python3
"""Resume of the CritterLifegain cast-order A/B under the box-wide batch lock (RULES.txt, 2026-10-15).

The lock admits ONE batch at a time, and a batch must pool ALL its arms. Day 1 ran the two arms as two
binaries side by side (mtg.base, mtg.new) and finished critter shipped (4000 paired games) and 518 critter2hg
shipped games before it was stopped. Today:

  batch A (mtg.new):  every remaining critter cell with BOTH arms in one process -- `new` (default) and `old`
                      = MTG_CRITTER_ORDER=0 per job, which is byte-identical to ff3e205b (day-1 hatch 400/400,
                      re-verified per cell by batch B) -- plus the identity jobs day 1 did not reach (new side).
  batch B (mtg.base): the identity jobs day 1 did not reach (base side) + the =0 check slices (the first
                      chunks of every batch-A cell on the real base binary).

  python3 -I make_ab2.py build A OUT   |   python3 -I make_ab2.py build B OUT
  python3 -I make_ab2.py analyse DAY1_BASE DAY1_NEW A_DIR B_DIR      -> tables + A_DIR/moved.json

Seeds and job naming are day 1's (make_ab.py): seed(game gi) = 11200001 + 100000 k + 30000 c + gi (critter k=0,
critter2hg k=1; cells ship 0, d3 1, d0 2); identity 11500001 + 1000 deck_index + 300 c + gi. Chunks of 50.
"""
import json, math, os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab as M

DONE_IDENT = {"antilife_ship", "auras_ship", "bruna_ship", "burn_ship", "creature_giving_ship",
              "fivecolour_ship", "goblins_ship", "hinata_ship", "stompy_ship"}
# Batch-A critter cells: (key, cell) -> games. critter shipped finished on day 1; critter2hg shipped is re-run
# whole (its first 518 games then double as an =0 check against day 1's base binary).
A_CELLS = [("critter2hg", "ship", 2000), ("critter", "d3", 6000), ("critter2hg", "d3", 3000),
           ("critter", "d0", 20000), ("critter2hg", "d0", 10000)]
CHECK = {"ship": 300, "d3": 600, "d0": 2000}   # batch B's =0 check slice per cell (critter2hg: half)
CELLDEF = {c[0]: c for c in M.CELLS}


def critter_jobs(key, cell, n, arm, flags):
    _, depth, bud = CELLDEF[cell]
    extra = {"starting_life": 30, "opponent_heads": 2} if key == "critter2hg" else None
    base = 11200001 + 100000 * (0 if key == "critter" else 1) + 30000 * M.CELL_IX[cell]
    return [M.job("%s_%s_%s.c%d" % (key, cell, arm, c), M.CRITTER[0], M.CRITTER[1], min(M.CHUNK, n - c),
                  base + c, c, depth, bud, extra, flags) for c in range(0, n, M.CHUNK)]


def ident_jobs(arm):
    jobs = []
    for di, (key, deck, prof) in enumerate(M.suite_decks()):
        root = M.root_for(prof)
        for cname, depth, bud in M.CELLS:
            if "%s_%s" % (key, cname) in DONE_IDENT:
                continue
            base = 11500001 + 1000 * di + 300 * M.CELL_IX[cname]
            jobs.append(M.job("ident-%s_%s_%s.c0" % (key, cname, arm), deck, prof, M.IDENT_GAMES[cname], base, 0,
                              depth, bud, root=root))
    return jobs


def build(which, out):
    jobs = []
    if which == "A":
        jobs += ident_jobs("new")
        for key, cell, n in A_CELLS:
            jobs += critter_jobs(key, cell, n, "new", None)
            jobs += critter_jobs(key, cell, n, "old", {"MTG_CRITTER_ORDER": False})
    else:
        jobs += ident_jobs("base")
        for key, cell, n in A_CELLS:
            m = CHECK[cell] if key == "critter" else CHECK[cell] // 2
            jobs += critter_jobs(key, cell, m, "base", None)
    # Slowest first: the identity decks' searched jobs, then the searched critter cells, then d0.
    def pri(j):
        if j["name"].startswith("ident-") and j.get("depth", 5) > 0:
            return 0
        if j.get("depth", 5) > 0:
            return 1
        return 2
    jobs.sort(key=pri)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s/manifest.json: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


def paired(a, b):
    gis = sorted(set(a) & set(b))
    dd = [a[g] - b[g] for g in gis]
    n = len(dd)
    if n == 0:
        return None
    mean = sum(dd) / n
    var = sum((v - mean) ** 2 for v in dd) / max(1, n - 1)
    se = math.sqrt(var / n) if n > 1 else 0.0
    return dict(n=n, a=sum(a[g] for g in gis) / n, b=sum(b[g] for g in gis) / n, mean=mean,
                t=mean / se if se > 0 else 0.0, faster=sum(1 for v in dd if v < 0),
                slower=sum(1 for v in dd if v > 0), net=sum(dd), gis=gis)


def per_arm(wins, jobs):
    per = defaultdict(dict)                 # (key, cell, arm) -> gi -> wt
    cost = defaultdict(lambda: [0, 0])      # (key, cell, arm) -> [ms, units]
    dig = {}                                # (ident?, key, cell, arm, chunk) -> (digest, avg)
    for name, g in wins.items():
        sp = M.split(name)
        if sp and not sp[0]:
            per[(sp[1], sp[2], sp[3])].update(g)
    for name, j in jobs.items():
        sp = M.split(name)
        if not sp:
            continue
        if not sp[0]:
            c = cost[(sp[1], sp[2], sp[3])]
            c[0] += j["ms"]
            c[1] += j["units"]
        dig[(sp[0], sp[1], sp[2], sp[3], sp[4])] = (j["digest"], j["avg"], j["units"])
    return per, cost, dig


def analyse(d1b, d1n, adir, bdir):
    P = {}
    C = {}
    D = {}
    for tag, d in (("d1b", d1b), ("d1n", d1n), ("A", adir), ("B", bdir)):
        w, j = M.load(d)
        P[tag], C[tag], D[tag] = per_arm(w, j)
    rows = []
    # critter shipped: day 1, real base binary vs new.
    rows.append(("critter", "ship", P["d1n"][("critter", "ship", "new")], P["d1b"][("critter", "ship", "base")],
                 C["d1n"][("critter", "ship", "new")], C["d1b"][("critter", "ship", "base")], "day1 base binary"))
    for key, cell, n in A_CELLS:
        rows.append((key, cell, P["A"][(key, cell, "new")], P["A"][(key, cell, "old")],
                     C["A"][(key, cell, "new")], C["A"][(key, cell, "old")], "=0 arm, same process"))
    print("A/B: new (MTG_CRITTER_ORDER default ON) vs old (ff3e205b / its byte-identical =0 hatch), paired; unwon = 9")
    print("%-11s %-5s %6s %8s %8s %9s %7s %6s %6s %6s %13s %8s  %s" % (
        "deck", "cell", "n", "old", "new", "new-old", "t", "faster", "slower", "net", "units old/new", "x", "old arm"))
    slower, deck_net = [], defaultdict(int)
    for key, cell, pn, po, cn, co, src in rows:
        r = paired(pn, po)
        if r is None:
            print("%-11s %-5s (no data)" % (key, cell))
            continue
        deck_net[key] += r["net"]
        print("%-11s %-5s %6d %8.4f %8.4f %+9.5f %7.2f %6d %6d %+6d %6.2fM/%6.2fM %8s  %s" % (
            key, cell, r["n"], r["b"], r["a"], r["mean"], r["t"], r["faster"], r["slower"], r["net"],
            co[1] / 1e6, cn[1] / 1e6, ("%.3f" % (cn[1] / co[1])) if co[1] else "-", src))
        for g in r["gis"]:
            if pn[g] > po[g]:
                slower.append((key, cell, g, po[g], pn[g],
                               11200001 + 100000 * (0 if key == "critter" else 1) + 30000 * M.CELL_IX[cell] + g))
    print("\nPER-DECK NET (all cells; bar <= 0):")
    for k, v in deck_net.items():
        print("  %-11s %+d" % (k, v))
    # =0 identity: day-1 hatch vs day-1 base; batch-B base slices vs batch-A old; day-1 2HG ship base vs A old.
    def cmp_chunks(da, arma, db, armb, only_critter=True):
        tot = same = 0
        diffs = []
        for (ident, key, cell, arm, c), v in da.items():
            if ident or arm != arma:
                continue
            w = db.get((False, key, cell, armb, c))
            if w is None:
                continue
            tot += 1
            if v[0] == w[0]:
                same += 1
            else:
                diffs.append((key, cell, c, v, w))
        return tot, same, diffs
    for label, da, aa, db, ab in (("day-1 hatch (new binary =0) vs day-1 base", D["d1n"], "hatch", D["d1b"], "base"),
                                  ("batch-A old (=0) vs batch-B base binary", D["A"], "old", D["B"], "base"),
                                  ("batch-A old (=0) vs day-1 base (2HG shipped)", D["A"], "old", D["d1b"], "base"),
                                  ("batch-A new (mtg.diag) vs day-1 new (mtg.new), 2HG shipped",
                                   D["A"], "new", D["d1n"], "new")):
        tot, same, diffs = cmp_chunks(da, aa, db, ab)
        print("\n=0 IDENTITY, %s: %d/%d chunk digests identical" % (label, same, tot))
        for x in diffs[:10]:
            print("   DIFF", x)
    # Identity decks: new vs base, day 1 + batches A/B.
    itot = isame = 0
    idiff, udiff = [], []
    for dn, db in ((D["d1n"], D["d1b"]), (D["A"], D["B"])):
        for (ident, key, cell, arm, c), v in dn.items():
            if not ident:
                continue
            w = db.get((True, key, cell, "base", c))
            if w is None:
                idiff.append((key, cell, "missing on base"))
                continue
            itot += 1
            if v[0] == w[0] and v[1] == w[1]:
                isame += 1
            else:
                idiff.append((key, cell, w, v))
            if v[2] != w[2]:
                udiff.append((key, cell, w[2], v[2]))
    print("\nIDENTITY (every other suite deck, base binary vs new binary): %d/%d jobs identical (digest + avg)"
          % (isame, itot))
    for x in idiff:
        print("   IDENT DIFF", x)
    for x in udiff:
        print("   units differ (play identical)", x)
    print("\nEVERY SLOWER game (deck cell gi old new seed):")
    for s in slower:
        print("  %-11s %-5s gi=%-6d old=%d new=%d seed=%d" % s)
    json.dump({"slower": slower}, open(os.path.join(adir, "moved.json"), "w"))


if __name__ == "__main__":
    if sys.argv[1] == "build":
        build(sys.argv[2], sys.argv[3])
    else:
        analyse(*sys.argv[2:6])
