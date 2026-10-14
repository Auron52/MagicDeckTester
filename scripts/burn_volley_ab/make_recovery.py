#!/usr/bin/env python3
"""Two-stage recovery check for every SLOWER game of a candidate arm (USER 2026-10-05):
stage 1 = the slower arm's game at --depth W --budget-ms 100 (W = the faster arm's win turn; a loss -> 8);
stage 2 = only the stage-1 failures, at --depth 8 --budget-ms 0. A game recovers when the arm wins by W.

  python3 make_recovery.py --ab AB_DIR --out DIR --stage 1 [--vs cur] [--arms k1,k2,...]
  python3 make_recovery.py --ab AB_DIR --out DIR --stage 2 --prev STAGE1_DIR
  -> run_batch.sh <mtg> DIR ;  python3 make_recovery.py --analyse DIR
"""
import argparse, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab  # noqa: E402

FLAGS = dict(make_ab.ARMS)


def slower_games(ab, vs, arms):
    wins = make_ab.load(ab)
    seeds = json.load(open(os.path.join(ab, "design.json")))["seeds"]
    out = []
    for (cell, sset, arm), w in sorted(wins.items()):
        if arm == vs or (arms and arm not in arms):
            continue
        base = wins.get((cell, sset, vs), {})
        for gi in sorted(w):
            if gi in base and w[gi] > base[gi]:
                out.append({"cell": cell, "set": sset, "arm": arm, "gi": gi, "turn": w[gi], "vs_turn": base[gi],
                            "W": base[gi] if base[gi] < 9 else 8, "seed": seeds["%s_%s" % (cell, sset)] + gi})
    return out


def job(g, depth, budget):
    j = {"name": "rec_%s_%s_%s_gi%d_W%d" % (g["cell"], g["set"], g["arm"], g["gi"], g["W"]),
         "deck": os.path.join(make_ab.ROOT, make_ab.DECK), "profile": os.path.join(make_ab.ROOT, make_ab.PROF),
         "games": 1, "seed": g["seed"], "game_index": g["gi"], "depth": depth, "budget_ms": budget,
         "ignore_play_profile": True}
    if FLAGS[g["arm"]]:
        j["flags"] = dict(FLAGS[g["arm"]])
    return j


def build(a):
    os.makedirs(a.out, exist_ok=True)
    if a.stage == 1:
        games = slower_games(a.ab, a.vs, a.arms.split(",") if a.arms else [])
        jobs = [job(g, g["W"], 100) for g in games]
    else:
        prev = json.load(open(os.path.join(a.prev, "games.json")))
        res = analyse(a.prev, quiet=True)
        games = [g for g in prev if not res.get(job(g, 0, 0)["name"].replace("rec_", "", 1), False)]
        jobs = [job(g, 8, 0) for g in games]
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump(games, open(os.path.join(a.out, "games.json"), "w"), indent=1)
    print("wrote %s: %d jobs" % (a.out, len(jobs)))


def analyse(d, quiet=False):
    games = json.load(open(os.path.join(d, "games.json")))
    pat = re.compile(r"\[win\] job=rec_(\S+) gi=(\d+) wt=(-?\d+)")
    got = {}
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            got[m.group(1)] = int(m.group(3))
    res = {}
    for g in games:
        key = "%s_%s_%s_gi%d_W%d" % (g["cell"], g["set"], g["arm"], g["gi"], g["W"])
        wt = got.get(key)
        ok = wt is not None and 0 < wt <= g["W"]
        res[key] = ok
        if not quiet:
            print("  %-4s %-3s %-8s gi=%-6d seed=%-9d arm=%d vs=%d W=%d -> %s %s" % (
                g["cell"], g["set"], g["arm"], g["gi"], g["seed"], g["turn"], g["vs_turn"], g["W"],
                "T%d" % wt if wt is not None and wt > 0 else ("unwon" if wt is not None else "MISSING"),
                "RECOVERS" if ok else "no"))
    if not quiet:
        print("recovered %d / %d" % (sum(res.values()), len(res)))
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--ab")
    ap.add_argument("--out")
    ap.add_argument("--stage", type=int, default=1)
    ap.add_argument("--prev")
    ap.add_argument("--vs", default="cur")
    ap.add_argument("--arms", default="")
    ap.add_argument("--analyse")
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
