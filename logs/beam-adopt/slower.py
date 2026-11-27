#!/usr/bin/env python3
"""Slower-game checks for the Task 6 A/B. USER bar: a slower game must recover at --depth 8 --budget-ms 0 (turn <= B's
play turn), or be clairvoyant (MTG_NC_SEARCH=1: both binaries at the play setting give F <= B); otherwise REAL.
At depth 8 neither binary runs a beam (the beam fires at the play depth / the d3 regime only), so one d8 b0 replay on F
serves both arms.
  slower.py mk <abdir>        -> <abdir>/sl/d8.json (F binary), nc_B.json, nc_F.json (run with MTG_NC_SEARCH=1)
  slower.py verdict <abdir>"""
import copy, json, os, sys
ab = sys.argv[2]; sl = os.path.join(ab, "sl")
def wins(d):
    W = {}
    for f in os.listdir(d):
        if f.endswith(".wins"):
            W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1])) for p in (l.split() for l in open(os.path.join(d, f))) if len(p) >= 2}
    return W
def single(j, gi, name):
    x = copy.deepcopy(j); x.update({"name": name, "games": 1, "seed": j["seed"] + gi, "game_index": gi}); return x
rows = [r["row"] for r in json.load(open(os.path.join(ab, "rows.json")))["rows"]]
JB = {j["name"]: j for j in json.load(open(os.path.join(ab, "ab_B.json")))["jobs"]}
WB, WF = wins(os.path.join(ab, "wins_B")), wins(os.path.join(ab, "wins_F"))
S = []
for nb, wb in WB.items():
    nf = nb.replace("_B_s", "_F_s")
    for gi, tb in wb.items():
        tf = WF.get(nf, {}).get(gi)
        if tf is not None and tf > tb:
            S.append(dict(row=nb.split("_B_s")[0], seed=JB[nb]["seed"], gi=gi, tb=tb, tf=tf, job=JB[nb]))
if sys.argv[1] == "mk":
    os.makedirs(sl, exist_ok=True)
    d8, nb_, nf_ = [], [], []
    for s in S:
        tag = f"{s['row']}_s{s['seed']}_g{s['gi']}"
        x = single(s["job"], s["gi"], tag + "_d8")
        x.update({"depth": 8, "budget_ms": 0, "ignore_play_profile": True, "abandon_units": 40000000}); d8.append(x)
        nb_.append(single(s["job"], s["gi"], tag + "_ncB")); nf_.append(single(s["job"], s["gi"], tag + "_ncF"))
    for n, js in (("d8", d8), ("nc_B", nb_), ("nc_F", nf_)):
        json.dump({"jobs": js}, open(os.path.join(sl, n + ".json"), "w"), indent=1)
    print(len(S), "slower games")
else:
    D8 = wins(os.path.join(sl, "w_d8")) if os.path.isdir(os.path.join(sl, "w_d8")) else {}
    NB = wins(os.path.join(sl, "w_ncB")) if os.path.isdir(os.path.join(sl, "w_ncB")) else {}
    NF = wins(os.path.join(sl, "w_ncF")) if os.path.isdir(os.path.join(sl, "w_ncF")) else {}
    tally = {}
    for s in S:
        tag = f"{s['row']}_s{s['seed']}_g{s['gi']}"
        d8 = D8.get(tag + "_d8", {}).get(s["gi"]); b = NB.get(tag + "_ncB", {}).get(s["gi"]); f = NF.get(tag + "_ncF", {}).get(s["gi"])
        v = ("recover" if d8 is not None and d8 <= s["tb"] else "clairvoyant" if (b is not None and f is not None and f <= b)
             else "pending" if None in (d8, b, f) else "REAL")
        tally[v] = tally.get(v, 0) + 1
        print(f"{s['row']:20s} seed {s['seed']} gi {s['gi']:4d}: B {s['tb']} -> F {s['tf']}  d8b0 {d8}  NC B {b} F {f}  => {v}")
    print("tally:", tally)
