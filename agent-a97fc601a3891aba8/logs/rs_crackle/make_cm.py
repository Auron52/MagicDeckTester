#!/usr/bin/env python3
"""Held-out pooled batch for MTG_HINATA_CASTABLE_MANA and MTG_RESTRICTOR_ORDER_EXACT (seeds 8300001+).

    python3 make_cm.py build <dir>          -> <dir>/manifest.json, Hinata2, three arms per cell:
         base = shipped defaults (both levers on); moff = HINATA_CASTABLE_MANA off; roff = RESTRICTOR_ORDER_EXACT off
    python3 make_cm.py build-ident <dir>    -> Hinata jobs with NO flags block (for the 3d64d9a6 binary) -- the
                                               new binary runs them with both levers =0 in the environment
    python3 make_cm.py build-other <dir>    -> burn / dragonstorm / stompy, no flags (defaults: levers ON)
    python3 make_cm.py analyse <dir>        -> lever M = base vs moff, lever R = base vs roff
    python3 make_cm.py ident <dirA> <dirB>  -> per-job digest comparison
Pairs by gi; a game's deal seed = cell base + gi. Unwon scores 9.
"""
import json, math, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
DECK = WT + "/decks/Hinata2/Hinata2.cod"
PROF = WT + "/decks/Hinata2/Hinata2.profile.json"
M, R = "MTG_HINATA_CASTABLE_MANA", "MTG_RESTRICTOR_ORDER_EXACT"
CELLS = [  # name, depth (None = profile), budget, games, seed base, chunk, extra
    ("ship", None, 20, 200, 8320001, 25, {}),
    ("d3", 3, 10, 200, 8310001, 25, {}),
    ("2hg", 0, 0, 400, 8330001, 50, {"starting_life": 30, "opponent_heads": 2}),
    ("d0", 0, 0, 2000, 8300001, 50, {}),
]
ARMS = [("base", {M: True, R: True}), ("moff", {M: False, R: True}), ("roff", {M: True, R: False})]
OTHER = [("burn", "decks/burn/burn.txt", "decks/burn/burn.profile.json"),
         ("dragonstorm", "decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
         ("stompy", "decks/StompySurprise/StompySurprise.cod", "decks/StompySurprise/StompySurprise.profile.json")]


def jobs_for(arms, with_flags=True, cap=None):
    jobs = []
    for cname, depth, bud, games, base, chunk, extra in CELLS:
        lim = games if cap is None else min(games, cap.get(cname, games))
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


def other_jobs():
    jobs = []
    for key, deck, prof in OTHER:
        for cell, depth, bud, g in (("d0", 0, 0, 100), ("d3", 3, 10, 30)):
            jobs.append({"name": "%s_%s_x.c0" % (key, cell), "deck": WT + "/" + deck, "profile": WT + "/" + prof,
                         "games": g, "seed": 8350001, "game_index": 0, "budget_ms": bud, "depth": depth,
                         "ignore_play_profile": True})
    return jobs


def write(out, jobs):
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


def parse(out):
    wins = defaultdict(dict)
    pat = re.compile(r"\[win\] job=(\w+?)_(base|moff|roff)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(out, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(5))
            wins[(m.group(1), m.group(2))][int(m.group(4))] = 9 if wt < 0 else wt
    units = defaultdict(int); digests = {}
    jp = re.compile(r"^(\S+?): played=(\d+) avg=\S+ digest=(\S+) ms=(\d+) units=(\d+)")
    for line in open(os.path.join(out, "batch.out"), errors="ignore"):
        m = jp.search(line)
        if m:
            digests[m.group(1)] = m.group(3)
            mm = re.match(r"(\w+?)_(base|moff|roff)\.c\d+$", m.group(1))
            if mm:
                units[(mm.group(1), mm.group(2))] += int(m.group(5))
    return wins, units, digests


def analyse(out):
    wins, units, _ = parse(out)
    for lever, x, y in (("M  HINATA_CASTABLE_MANA on vs off", "base", "moff"),
                        ("R  RESTRICTOR_ORDER_EXACT on vs off", "base", "roff")):
        print("\n== %s" % lever)
        print("%-5s %5s %8s %8s %7s %6s %6s %6s %8s" % ("cell", "n", "sum_on", "sum_off", "d_mean", "t", "faster", "slower", "units"))
        slower = []
        tot = [0, 0, 0, 0]
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
            tot[0] += n; tot[1] += sum(d); tot[2] += f; tot[3] += s_
            for g in gis:
                if a[g] > b[g]:
                    slower.append((cname, g, b[g], a[g], base + g))
        print("deck net: n=%d  sum(on-off)=%+d  faster=%d slower=%d" % tuple(tot))
        print("SLOWER (on > off): cell gi off on seed")
        for r in slower:
            print("  %-5s gi=%-5d off=%d on=%d seed=%d" % r)


def ident(da, db):
    _, _, A_ = parse(da)
    _, _, B_ = parse(db)
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
        write(sys.argv[2], jobs_for([("base", {})], with_flags=False,
                                    cap={"ship": 50, "d3": 50, "2hg": 400, "d0": 1000}))
    elif cmd == "build-other":
        write(sys.argv[2], other_jobs())
    elif cmd == "analyse":
        analyse(sys.argv[2])
    elif cmd == "ident":
        ident(sys.argv[2], sys.argv[3])
