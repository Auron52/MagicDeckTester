#!/usr/bin/env python3
"""CPU time vs quality, PER DECK, for MTG_DIG_SEARCHED_CHOICE: arms 'dig0' (=0, the committed tree) and 'new'
(lever on) across a budget ladder at the deck's shipped depth and at d3, in ONE pooled batch on fresh seeds.

Every cell of a deck uses the SAME seeds (seed0 + 20000 * deck_index + gi), so the comparisons are paired
across arms AND across budgets. Quality = mean turn-to-win with a loss scored 9 (lower is better). CPU =
core-ms per game (the batch's per-game wall-time sum; run on an idle box with workers <= physical cores) and
deterministic search units per game. Chunks of the two arms are interleaved so both see the same machine.

  python3 make_cpu_quality.py --out DIR [--seed0 9700001] [--budgets 5,10,20,40,80]
  -> run:  MTG_DUMP_WINS=1 mtg --batch DIR/manifest.json --threads 12 > DIR/batch.out 2> DIR/batch.err
  python3 make_cpu_quality.py --analyse DIR
"""
import argparse, json, math, os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_dig_ab import ALL_DECKS, KEYS, PRIMARY  # noqa: E402

ARMS = [("dig0", {"MTG_DIG_SEARCHED_CHOICE": False}), ("new", {})]
SHIP_DEPTH = {"fivecolour": 6}          # value_play depth (5 for the rest), as the shipped cell resolved it
GAMES = {"th": 1200, "edf": 100}         # default 600
CHUNK = {"edf": 25}                      # default 50
# EDF costs ~35 core-s per game at its shipped depth: a reduced ladder (its play was byte-identical between
# the arms at every cell of the 2026-10-14 A/B).
LADDER = {"edf": {"ship": [20], "d3": [10, 20]}}


def cells(key, budgets):
    lad = LADDER.get(key, {"ship": budgets, "d3": budgets})
    out = []
    for cname in ("ship", "d3"):
        depth = SHIP_DEPTH.get(key, 5) if cname == "ship" else 3
        for b in lad[cname]:
            out.append((cname, depth, b))
    return out


def build(a):
    budgets = [int(b) for b in a.budgets.split(",")]
    jobs = []
    for key, deck, prof in ALL_DECKS:
        n = GAMES.get(key, 600)
        ch = CHUNK.get(key, 50)
        base = a.seed0 + 20000 * KEYS.index(key)
        for cname, depth, bud in cells(key, budgets):
            for c in range(0, n, ch):
                for arm, fl in ARMS:          # interleaved: the two arms' chunk c run back to back
                    j = {"name": "%s_%s%d_b%d_%s.c%d" % (key, cname, depth, bud, arm, c),
                         "deck": os.path.join(PRIMARY, deck), "profile": os.path.join(PRIMARY, prof),
                         "games": min(ch, n - c), "seed": base + c, "game_index": c, "budget_ms": bud,
                         "depth": depth, "ignore_play_profile": True}
                    if fl:
                        j["flags"] = dict(fl)
                    jobs.append(j)
    # Most expensive jobs first so the tail is short (cost ~ budget x depth; EDF and FiveColour lead).
    weight = {"edf": 30.0, "fivecolour": 6.0}
    jobs.sort(key=lambda j: -(j["budget_ms"] * (2.0 if j["depth"] > 3 else 1.0)
                              * weight.get(j["name"].split("_")[0], 1.0)))
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump({"seed0": a.seed0}, open(os.path.join(a.out, "seeds.json"), "w"))
    print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))


def interp_quality(points, cpu):
    """Quality at a given CPU by linear interpolation in log(CPU) over a sorted (cpu, quality) list; None
    outside the measured range."""
    pts = sorted(points)
    for (c0, q0), (c1, q1) in zip(pts, pts[1:]):
        if c0 <= cpu <= c1:
            if c1 == c0:
                return q0
            t = (math.log(cpu) - math.log(c0)) / (math.log(c1) - math.log(c0))
            return q0 + t * (q1 - q0)
    return None


def analyse(d):
    seed0 = json.load(open(os.path.join(d, "seeds.json")))["seed0"]
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3)(\d+)_b(\d+)_([a-z0-9]+)\.c\d+ gi=(\d+) wt=(-?\d+)")
    wins = defaultdict(dict)
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(7))
            wins[(m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), m.group(5))][int(m.group(6))] = \
                9 if wt < 0 else wt
    cost = defaultdict(lambda: [0, 0, 0])
    jpat = re.compile(r"^(\S+?)_(ship|d3)(\d+)_b(\d+)_([a-z0-9]+)\.c\d+: played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+)")
    for line in open(os.path.join(d, "batch.out"), errors="ignore"):
        m = jpat.search(line)
        if m:
            c = cost[(m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), m.group(5))]
            c[0] += int(m.group(6))
            c[1] += int(m.group(7))
            c[2] += int(m.group(8))
    summary = []
    for key in KEYS:
        rows = sorted({(k[1], k[2], k[3]) for k in wins if k[0] == key}, key=lambda t: (t[0] != "ship", t[2]))
        if not rows:
            continue
        print("\n=== %s ===" % key)
        print("%-5s %3s %4s %5s | %8s %9s %7s | %8s %9s %7s | %9s %5s %5s %4s | %6s %6s" % (
            "cell", "d", "b", "n", "dig0 q", "dig0 ms/g", "ku/g", "new q", "new ms/g", "ku/g",
            "diff", "fast", "slow", "net", "cpu x", "unit x"))
        curves = defaultdict(lambda: defaultdict(list))
        for cname, depth, bud in rows:
            w0, w1 = wins.get((key, cname, depth, bud, "dig0"), {}), wins.get((key, cname, depth, bud, "new"), {})
            gis = sorted(set(w0) & set(w1))
            if not gis:
                continue
            n = len(gis)
            q0 = sum(w0[g] for g in gis) / n
            q1 = sum(w1[g] for g in gis) / n
            dd = [w1[g] - w0[g] for g in gis]
            c0, c1 = cost[(key, cname, depth, bud, "dig0")], cost[(key, cname, depth, bud, "new")]
            ms0 = c0[1] / c0[0] if c0[0] else float("nan")
            ms1 = c1[1] / c1[0] if c1[0] else float("nan")
            u0 = c0[2] / c0[0] / 1000 if c0[0] else float("nan")
            u1 = c1[2] / c1[0] / 1000 if c1[0] else float("nan")
            print("%-5s %3d %4d %5d | %8.4f %9.1f %7.1f | %8.4f %9.1f %7.1f | %+9.4f %5d %5d %+4d | %6.3f %6.3f" % (
                cname, depth, bud, n, q0, ms0, u0, q1, ms1, u1, q1 - q0, sum(1 for v in dd if v < 0),
                sum(1 for v in dd if v > 0), sum(dd), ms1 / ms0 if ms0 else float("nan"),
                u1 / u0 if u0 else float("nan")))
            curves[(cname, depth)]["dig0"].append((ms0, q0))
            curves[(cname, depth)]["new"].append((ms1, q1))
            summary.append((key, cname, depth, bud, n, q0, q1, ms0, ms1, sum(dd)))
        # Matched-CPU read: at each lever-off point, the lever-on curve's quality at the SAME CPU, and at each
        # lever-on point, the lever-off curve's. Positive = the lever arm is worse at equal CPU.
        for (cname, depth), cv in sorted(curves.items()):
            if len(cv["dig0"]) < 2:
                continue
            out = []
            for ms0, q0 in sorted(cv["dig0"]):
                q1 = interp_quality(cv["new"], ms0)
                if q1 is not None:
                    out.append("%.0fms: new %+.4f" % (ms0, q1 - q0))
            print("  matched CPU (%s d%d), new - dig0 at the dig0 point's CPU: %s" % (
                cname, depth, "; ".join(out) if out else "no overlap"))
    json.dump(summary, open(os.path.join(d, "summary.json"), "w"))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--seed0", type=int, default=9700001)
    ap.add_argument("--budgets", default="5,10,20,40,80")
    ap.add_argument("--analyse")
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
