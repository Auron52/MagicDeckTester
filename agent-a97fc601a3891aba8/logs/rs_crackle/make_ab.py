#!/usr/bin/env python3
"""ONE pooled batch: MTG_CRACKLE_COUNT_EXACT on vs off, paired by gi, fresh seeds >= 7900001.

    python3 make_ab.py build <out_dir>            -> <out_dir>/manifest.json (both arms)
    python3 make_ab.py build-ident <out_dir>      -> the OFF arm's jobs WITHOUT a flags block (for the
                                                     ccacda4e binary, which does not know the lever; run
                                                     the new binary on it with MTG_CRACKLE_COUNT_EXACT=0)
    python3 make_ab.py analyse <out_dir>          -> per-cell table + every slower game's repro
    python3 make_ab.py ident <dirA> <dirB>        -> per-job digest comparison of two batch.out files

Cells (Hinata2 only -- no other deck holds a discount_targets_scale_x card):
  d0   depth 0 (runner), 400 games
  d3   depth 3 budget 10, 150 games
  ship profile value_play depth, budget 20, 100 games
  2hg  depth 0, starting life 30, two opponent heads, 100 games (the hold rule's face_hits branch)
A game's deal = job seed + local game number, game index = job game_index + local number, so the
arms pair by gi. Unwon scores 9.
"""
import json, math, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
DECK = WT + "/decks/Hinata2/Hinata2.cod"
PROF = WT + "/decks/Hinata2/Hinata2.profile.json"
LEVER = "MTG_CRACKLE_COUNT_EXACT"
CELLS = [  # name, depth (None = profile), budget, games, seed base, extra
    ("d0", 0, 0, 400, 7900001, {}),
    ("d3", 3, 10, 150, 7910001, {}),
    ("ship", None, 20, 100, 7920001, {}),
    ("2hg", 0, 0, 100, 7930001, {"starting_life": 30, "opponent_heads": 2}),
]
CHUNK = 25
ARMS = [("on", True), ("off", False)]


def jobs_for(arms, with_flags=True):
    jobs = []
    for cname, depth, bud, games, base, extra in CELLS:
        for c in range(0, games, CHUNK):
            for arm, val in arms:
                j = {"name": "%s_%s.c%d" % (cname, arm, c), "deck": DECK, "profile": PROF,
                     "games": min(CHUNK, games - c), "seed": base + c, "game_index": c, "budget_ms": bud}
                if with_flags:
                    j["flags"] = {LEVER: val}
                if depth is not None:
                    j["depth"] = depth
                    j["ignore_play_profile"] = True
                j.update(extra)
                jobs.append(j)
    # Expensive cells first so the pool's tail is the cheap d0 games.
    order = {"ship": 0, "d3": 1, "2hg": 2, "d0": 3}
    jobs.sort(key=lambda j: order[j["name"].split("_")[0]])
    return jobs


def write(out, jobs):
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


def parse(out):
    wins = defaultdict(dict)    # (cell, arm) -> gi -> turns
    pat = re.compile(r"\[win\] job=(\w+?)_(on|off)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(out, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(5))
            wins[(m.group(1), m.group(2))][int(m.group(4))] = 9 if wt < 0 else wt
    units = defaultdict(int); ms = defaultdict(int); digests = {}
    jp = re.compile(r"^(\w+?)_(on|off)\.c(\d+): played=(\d+) avg=\S+ digest=(\S+) ms=(\d+) units=(\d+)")
    for line in open(os.path.join(out, "batch.out"), errors="ignore"):
        m = jp.search(line)
        if m:
            units[(m.group(1), m.group(2))] += int(m.group(7))
            ms[(m.group(1), m.group(2))] += int(m.group(6))
            digests["%s_%s.c%s" % (m.group(1), m.group(2), m.group(3))] = m.group(5)
    return wins, units, ms, digests


def analyse(out):
    wins, units, ms, _ = parse(out)
    print("%-5s %4s %8s %8s %7s %6s %6s %6s %10s %8s" % ("cell", "n", "sum_on", "sum_off", "d_mean", "t",
          "faster", "slower", "units_on/off", "ms on/off"))
    slower = []
    for cname, depth, bud, games, base, extra in CELLS:
        a, b = wins.get((cname, "on"), {}), wins.get((cname, "off"), {})
        gis = sorted(set(a) & set(b))
        if not gis:
            continue
        d = [a[g] - b[g] for g in gis]
        n = len(d); mean = sum(d) / n
        var = sum((v - mean) ** 2 for v in d) / max(1, n - 1)
        se = math.sqrt(var / n) if n > 1 else 0.0
        f = sum(1 for v in d if v < 0); s_ = sum(1 for v in d if v > 0)
        uo, uf = units[(cname, "on")], units[(cname, "off")]
        mo, mf = ms[(cname, "on")], ms[(cname, "off")]
        print("%-5s %4d %8d %8d %+7.4f %6.2f %6d %6d %10s %8s" % (
            cname, n, sum(a[g] for g in gis), sum(b[g] for g in gis), mean, mean / se if se else 0.0, f, s_,
            ("%.4f" % (uo / uf)) if uf else "n/a", ("%.3f" % (mo / mf)) if mf else "n/a"))
        for g in gis:
            if a[g] > b[g]:
                slower.append((cname, g, b[g], a[g], base + g))
    print("\nSLOWER (on > off): cell gi off on seed")
    for r in slower:
        print("  %-5s gi=%-5d off=%d on=%d seed=%d" % r)


def ident(da, db):
    _, _, _, A = parse(da)
    _, _, _, B = parse(db)
    keys = sorted(set(A) & set(B))
    diff = [k for k in keys if A[k] != B[k]]
    print("jobs compared: %d, digest mismatches: %d" % (len(keys), len(diff)))
    for k in diff:
        print("  MISMATCH", k, A[k], B[k])


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "build":
        write(sys.argv[2], jobs_for(ARMS))
    elif cmd == "build-ident":
        write(sys.argv[2], jobs_for([("off", False)], with_flags=False))
    elif cmd == "analyse":
        analyse(sys.argv[2])
    elif cmd == "ident":
        ident(sys.argv[2], sys.argv[3])
