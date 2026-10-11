#!/usr/bin/env python3
"""ONE pooled held-out batch for the two hold-rule levers (seeds 8100001+, disjoint from ab/).

    python3 make_hold.py build <dir>         -> <dir>/manifest.json, three arms per cell:
         base  = shipped defaults (HOLD_LETHAL_BURN on, HOLD_NEEDS_HINATA off)
         aoff  = HOLD_LETHAL_BURN off (and NEEDS_HINATA off)  == the 2cec6ec4 behaviour
         bon   = HOLD_NEEDS_HINATA on (HOLD_LETHAL_BURN at its default, on)
    python3 make_hold.py build-ident <dir>   -> the aoff jobs with NO flags block (for the 2cec6ec4 binary)
    python3 make_hold.py analyse <dir>       -> lever A = base vs aoff, lever B = bon vs base
    python3 make_hold.py ident <dirA> <dirB> -> per-job digest comparison (aoff vs 2cec6ec4)
Pairs by gi; a game's deal seed = cell base + gi. Unwon scores 9.
"""
import json, math, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
DECK = WT + "/decks/Hinata2/Hinata2.cod"
PROF = WT + "/decks/Hinata2/Hinata2.profile.json"
A, B = "MTG_HINATA_HOLD_LETHAL_BURN", "MTG_HINATA_HOLD_NEEDS_HINATA"
CELLS = [  # name, depth (None = profile), budget, games, seed base, chunk, extra
    ("ship", None, 20, 400, 8120001, 25, {}),
    ("d3", 3, 10, 400, 8110001, 25, {}),
    ("2hg", 0, 0, 400, 8130001, 50, {"starting_life": 30, "opponent_heads": 2}),
    ("d0", 0, 0, 3000, 8100001, 50, {}),
]
ARMS = [("base", {A: True, B: False}), ("aoff", {A: False, B: False}), ("bon", {A: True, B: True})]


def jobs_for(arms, with_flags=True, ident_cap=None):
    jobs = []
    for cname, depth, bud, games, base, chunk, extra in CELLS:
        lim = games if ident_cap is None else min(games, ident_cap.get(cname, games))
        for c in range(0, lim, chunk):
            for arm, fl in arms:
                j = {"name": "%s_%s.c%d" % (cname, arm, c), "deck": DECK, "profile": PROF,
                     "games": min(chunk, games - c), "seed": base + c, "game_index": c, "budget_ms": bud}
                if with_flags:
                    j["flags"] = dict(fl)
                if depth is not None:
                    j["depth"] = depth
                    j["ignore_play_profile"] = True
                j.update(extra)
                jobs.append(j)
    return jobs


def write(out, jobs):
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


def parse(out):
    wins = defaultdict(dict)
    pat = re.compile(r"\[win\] job=(\w+?)_(base|aoff|bon)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(out, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(5))
            wins[(m.group(1), m.group(2))][int(m.group(4))] = 9 if wt < 0 else wt
    units = defaultdict(int); ms = defaultdict(int); digests = {}
    jp = re.compile(r"^(\w+?)_(base|aoff|bon)\.c(\d+): played=(\d+) avg=\S+ digest=(\S+) ms=(\d+) units=(\d+)")
    for line in open(os.path.join(out, "batch.out"), errors="ignore"):
        m = jp.search(line)
        if m:
            units[(m.group(1), m.group(2))] += int(m.group(7))
            ms[(m.group(1), m.group(2))] += int(m.group(6))
            digests["%s_%s.c%s" % (m.group(1), m.group(2), m.group(3))] = m.group(5)
    return wins, units, ms, digests


def analyse(out):
    wins, units, ms, _ = parse(out)
    for lever, x, y in (("A  HOLD_LETHAL_BURN on vs off", "base", "aoff"), ("B  HOLD_NEEDS_HINATA on vs off", "bon", "base")):
        print("\n== %s  (arm %s vs %s)" % (lever, x, y))
        print("%-5s %5s %8s %8s %7s %6s %6s %6s %8s" % ("cell", "n", "sum_on", "sum_off", "d_mean", "t", "faster", "slower", "units"))
        slower = []
        for cname, depth, bud, games, base, chunk, extra in CELLS:
            a, b = wins.get((cname, x), {}), wins.get((cname, y), {})
            gis = sorted(set(a) & set(b))
            if not gis:
                continue
            d = [a[g] - b[g] for g in gis]
            n = len(d); mean = sum(d) / n
            var = sum((v - mean) ** 2 for v in d) / max(1, n - 1)
            se = math.sqrt(var / n) if n > 1 else 0.0
            f = sum(1 for v in d if v < 0); s_ = sum(1 for v in d if v > 0)
            uo, uf = units[(cname, x)], units[(cname, y)]
            print("%-5s %5d %8d %8d %+7.4f %6.2f %6d %6d %8s" % (
                cname, n, sum(a[g] for g in gis), sum(b[g] for g in gis), mean, mean / se if se else 0.0, f, s_,
                ("%.4f" % (uo / uf)) if uf > 1000 else "n/a"))
            for g in gis:
                if a[g] > b[g]:
                    slower.append((cname, g, b[g], a[g], base + g))
        print("SLOWER (%s > %s): cell gi %s %s seed" % (x, y, y, x))
        for r in slower:
            print("  %-5s gi=%-5d off=%d on=%d seed=%d" % r)


def ident(da, db):
    _, _, _, A_ = parse(da)
    _, _, _, B_ = parse(db)
    keys = sorted(set(A_) & set(B_))
    diff = [k for k in keys if A_[k] != B_[k]]
    print("jobs compared: %d, digest mismatches: %d" % (len(keys), len(diff)))
    for k in diff:
        print("  MISMATCH", k, A_[k], B_[k])


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "build":
        write(sys.argv[2], jobs_for(ARMS))
    elif cmd == "build-ident":
        write(sys.argv[2], jobs_for([("aoff", {})], with_flags=False,
                                    ident_cap={"ship": 100, "d3": 100, "2hg": 400, "d0": 1000}))
    elif cmd == "analyse":
        analyse(sys.argv[2])
    elif cmd == "ident":
        ident(sys.argv[2], sys.argv[3])
