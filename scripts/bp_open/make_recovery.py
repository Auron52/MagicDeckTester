#!/usr/bin/env python3
"""Recovery check for the slower games of an A/B (CLAUDE.md bar): every game the lever arm plays slower
must recover at stage 1 (--depth W --budget-ms 100, W = the faster arm's win turn) or stage 2
(--depth 8 --budget-ms 0), played with the LEVER arm.

  make_recovery.py build <ab-dir> <out-dir> --stage 1|2 [--arm on] [--base off] [--from <stage1-dir>]
     stage 1: every slower game of <ab-dir>/summary.json
     stage 2: the stage-1 games that did not recover (read from --from)
  -> run: MTG_DUMP_WINS=1 mtg --batch <out-dir>/manifest.json --threads 6
  make_recovery.py analyse <out-dir>
"""
import argparse, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ab import ALL_DECKS, ARMS, PRIMARY, seed_base  # noqa: E402

DECK = {k: (d, p) for k, d, p in ALL_DECKS}


def build(a):
    seeds = json.load(open(os.path.join(a.ab, "seeds.json")))
    seed0 = seeds["seed0"]
    rows = json.load(open(os.path.join(a.ab, "summary.json")))
    todo = []   # (key, cell, gi, faster_wt, slower_wt)
    if a.stage == 1:
        for r in rows:
            if r["arm"] != a.arm:
                continue
            for gi, w_base, w_arm in r["slower"]:
                todo.append((r["deck"], r["cell"], gi, w_base, w_arm))
    else:
        prev = json.load(open(os.path.join(a.frm, "results.json")))
        todo = [tuple(t) for t in prev if not t[5]]   # not recovered at stage 1
        todo = [t[:5] for t in todo]
    jobs = []
    for key, cell, gi, w_fast, w_slow in todo:
        deck, prof = DECK[key]
        depth = max(1, min(8, w_fast)) if a.stage == 1 else 8
        bud = 100 if a.stage == 1 else 0
        j = {"name": "%s_%s_%s_g%d_s%d" % (key, cell, a.arm, gi, a.stage), "deck": os.path.join(PRIMARY, deck),
             "profile": os.path.join(PRIMARY, prof), "games": 1, "seed": seed_base(seed0, key, cell) + gi,
             "game_index": gi, "budget_ms": bud, "depth": depth, "ignore_play_profile": True}
        if ARMS[a.arm]:
            j["flags"] = dict(ARMS[a.arm])
        jobs.append(j)
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump({"todo": todo, "stage": a.stage, "arm": a.arm}, open(os.path.join(a.out, "todo.json"), "w"), indent=1)
    print("wrote %d recovery jobs (stage %d)" % (len(jobs), a.stage))


def analyse(a):
    meta = json.load(open(os.path.join(a.out, "todo.json")))
    pat = re.compile(r"\[win\] job=(\S+)_g(\d+)_s(\d) gi=(\d+) wt=(-?\d+)")
    arm = meta.get("arm", "")
    got = {}
    for line in open(os.path.join(a.out, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(5))
            got[(m.group(1), int(m.group(2)))] = 9 if wt < 0 else wt
    res = []
    for key, cell, gi, w_fast, w_slow in meta["todo"]:
        wt = got.get(("%s_%s_%s" % (key, cell, arm), gi)) if arm else got.get(("%s_%s" % (key, cell), gi))
        rec = wt is not None and wt <= w_fast
        res.append([key, cell, gi, w_fast, w_slow, rec, wt])
        print("%-16s %-4s %-8s gi=%-4d faster=%d slower=%d -> stage%d %s %s" % (
            key, cell, arm, gi, w_fast, w_slow, meta["stage"], wt, "RECOVERED" if rec else "NOT recovered"))
    json.dump(res, open(os.path.join(a.out, "results.json"), "w"), indent=1)
    print("recovered %d / %d" % (sum(1 for r in res if r[5]), len(res)))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd")
    b = sub.add_parser("build")
    b.add_argument("ab"); b.add_argument("out")
    b.add_argument("--stage", type=int, default=1)
    b.add_argument("--arm", default="on")
    b.add_argument("--from", dest="frm", default="")
    an = sub.add_parser("analyse")
    an.add_argument("out")
    a = ap.parse_args()
    build(a) if a.cmd == "build" else analyse(a)
