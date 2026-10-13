#!/usr/bin/env python3
"""Two-stage recovery check (USER 2026-10-05) for every game where the lever-ON arm ('new') was SLOWER.

  stage 1:  python3 make_recovery.py --ab DIR --stage 1 --out OUT      -> OUT/manifest.json
            one job per slower game, the NEW arm (shipped default) at --depth W --budget-ms 100, W = the dig0
            arm's win turn (loss -> 8). A win by turn W is a real terminal line -> RECOVERED (conclusive).
  stage 2:  python3 make_recovery.py --ab DIR --stage 2 --prev OUT1 --out OUT2
            only the stage-1 failures, at --depth 8 --budget-ms 0.
  analyse:  python3 make_recovery.py --ab DIR --analyse OUT [--prev OUT1]
"""
import argparse, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_dig_ab as M

ap = argparse.ArgumentParser()
ap.add_argument("--ab", required=True)
ap.add_argument("--stage", type=int, default=1)
ap.add_argument("--out")
ap.add_argument("--prev")
ap.add_argument("--analyse")
a = ap.parse_args()

slower = json.load(open(os.path.join(a.ab, "slower.json")))
paths = {k: (os.path.join(M.PRIMARY, d), os.path.join(M.PRIMARY, p)) for k, d, p in M.ALL_DECKS}


def results(d):
    out = {}
    pat = re.compile(r"\[win\] job=rec_(\S+?)_(ship|d3|d0)_g(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(5))
            out[(m.group(1), m.group(2), int(m.group(3)))] = 9 if wt < 0 else wt
    return out


def target(row):
    deck, cell, gi, new, dig0, seed = row
    return dig0 if dig0 <= 8 else 8


if a.analyse:
    r = results(a.analyse)
    prev = results(a.prev) if a.prev else {}
    rec = fail = 0
    for row in slower:
        deck, cell, gi, new, dig0, seed = row
        w = target(row)
        key = (deck, cell, gi)
        got = r.get(key, prev.get(key))
        s1 = prev.get(key) if a.prev else r.get(key)
        ok = got is not None and got <= w
        rec += ok
        fail += (not ok)
        print("%-13s %-5s gi=%-5d seed=%d new=%d dig0=%d W=%d -> %s%s" % (
            deck, cell, gi, seed, new, dig0, w,
            "?" if got is None else str(got), "  RECOVERED" if ok else "  NOT RECOVERED"))
    print("recovered %d / %d" % (rec, rec + fail))
    sys.exit(0)

rows = slower
if a.stage == 2:
    r1 = results(a.prev)
    rows = [row for row in slower if not (r1.get((row[0], row[1], row[2])) is not None
                                          and r1[(row[0], row[1], row[2])] <= target(row))]
jobs = []
for row in rows:
    deck, cell, gi, new, dig0, seed = row
    w = target(row)
    d, p = paths[deck]
    jobs.append({"name": "rec_%s_%s_g%d" % (deck, cell, gi), "deck": d, "profile": p, "games": 1,
                 "seed": seed, "game_index": gi, "depth": (w if a.stage == 1 else 8),
                 "budget_ms": (100 if a.stage == 1 else 0), "ignore_play_profile": True})
os.makedirs(a.out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
print("wrote %s/manifest.json: %d jobs (stage %d)" % (a.out, len(jobs), a.stage))
