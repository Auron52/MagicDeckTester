#!/usr/bin/env python3
"""Slower-game checks for the Mill / K boundary-fix A/B. Bar: a slower game must recover at --depth 8 --budget-ms 0
(the new arm's flags; turn <= base's play turn); one that does not is replayed under MTG_NC_SEARCH=1 at the play
setting on both arms (new <= base => clairvoyant), otherwise REAL.
  slower.py mk <abdir> [cmp...]   -> <abdir>/sl/d8.json   (default cmps: KFvT KFvK)
  slower.py mknc <abdir> [cmp...] -> <abdir>/sl/nc.json   (only games whose d8 did not recover)
  slower.py verdict <abdir> [cmp...]"""
import copy, json, os, sys
mode, ab = sys.argv[1], sys.argv[2]; CMPS = sys.argv[3:] or ["KFvT", "KFvK"]; sl = os.path.join(ab, "sl")
def wins(d):
    W = {}
    if os.path.isdir(d):
        for f in os.listdir(d):
            if f.endswith(".wins"):
                W[f[:-5]] = {int(p[0]): (9 if int(p[1]) < 0 else int(p[1])) for p in (l.split() for l in open(os.path.join(d, f))) if len(p) >= 2}
    return W
JOBS = {j["name"]: j for j in json.load(open(os.path.join(ab, "ab.json")))["jobs"]}
def job(row, arm, seed): return JOBS[f"{row}_{arm}_s{seed}"]
def single(j, gi, name):
    x = copy.deepcopy(j); x.update({"name": name, "games": 1, "seed": j["seed"] + gi, "game_index": gi}); return x
S = [s for s in json.load(open(os.path.join(ab, "slower.json"))) if s["cmp"] in CMPS]
def tag(s): return f"{s['cmp']}_{s['row']}_s{s['seed']}_g{s['gi']}"
D8 = wins(os.path.join(sl, "w_d8")); NC = wins(os.path.join(sl, "w_nc"))
def d8of(s): return D8.get(tag(s) + "_d8", {}).get(s["gi"])
if mode == "mk":
    os.makedirs(sl, exist_ok=True); out = []
    for s in S:
        x = single(job(s["row"], s["new"], s["seed"]), s["gi"], tag(s) + "_d8")
        x.update({"depth": 8, "budget_ms": 0, "ignore_play_profile": True, "abandon_units": 40000000})
        out.append(x)
    json.dump({"jobs": out}, open(os.path.join(sl, "d8.json"), "w"), indent=1); print(len(out), "d8 jobs")
elif mode == "mknc":
    out = []
    for s in S:
        d8 = d8of(s)
        if d8 is not None and d8 <= s["tb"]: continue
        for arm in (s["base"], s["new"]):
            out.append(single(job(s["row"], arm, s["seed"]), s["gi"], tag(s) + f"_nc{arm}"))
    json.dump({"jobs": out}, open(os.path.join(sl, "nc.json"), "w"), indent=1); print(len(out), "nc jobs")
else:
    tally = {}
    for s in S:
        d8 = d8of(s)
        nb = NC.get(tag(s) + f"_nc{s['base']}", {}).get(s["gi"]); nn = NC.get(tag(s) + f"_nc{s['new']}", {}).get(s["gi"])
        if d8 is not None and d8 <= s["tb"]: v = "recover"
        elif nb is not None and nn is not None: v = "clairvoyant" if nn <= nb else "REAL"
        else: v = "pending"
        tally.setdefault(s["cmp"], {}); tally[s["cmp"]][v] = tally[s["cmp"]].get(v, 0) + 1
        nc = f"  NC {s['base']} {nb} {s['new']} {nn}" if nb is not None else ""
        print(f"{s['cmp']:6s} {s['row']:16s} seed {s['seed']} gi {s['gi']:4d}: {s['base']} {s['tb']} -> {s['new']} {s['tn']}  d8b0 {d8}{nc}  => {v}")
    print("tally:", tally)
