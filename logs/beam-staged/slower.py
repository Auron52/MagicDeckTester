#!/usr/bin/env python3
"""Slower-game classification for the beam-staged A/B (2026-11-25).

USER bar: every slower game must recover at --depth 8 --budget-ms 0, or be shown clairvoyant (MTG_NC_SEARCH=1 on
both arms at the play setting gives the same turn); anything else is REAL.

  slower.py mk <abdir> <arm,arm,...> <outdir>   writes <outdir>/d8.json (the arm's flags, depth 8 budget 0,
                                                ignore_play_profile) and <outdir>/nc.json (A and the arm at the job's
                                                own play setting; run it with MTG_NC_SEARCH=1) for every game where
                                                the arm is slower than A (a loss counts as 9).
  slower.py verdict <abdir> <outdir>            recover (d8b0 turn <= A's play turn) / clairvoyant (NC: arm == A) / real.
A single game of a batch job (seed S, game_index 0) is seed S+gi, game_index gi.
"""
import copy, json, os, sys


def wins(d):
    res = {}
    for f in os.listdir(d):
        if f.endswith(".wins"):
            for ln in open(os.path.join(d, f)):
                p = ln.split()
                if len(p) >= 2:
                    res.setdefault(f[:-5], {})[int(p[0])] = 9 if int(p[1]) < 0 else int(p[1])
    return res


def slower(ab, arms):
    rows = json.load(open(os.path.join(ab, "rows.json")))
    W = wins(os.path.join(ab, "wins"))
    jobs = {j["name"]: j for j in json.load(open(os.path.join(ab, "ab.json")))["jobs"]}
    out = []
    for r in rows["rows"]:
        row, eq = r["row"], rows["equiv"][r["row"]]
        for arm in arms:
            if arm not in eq or eq[arm] == eq["A"]:
                continue
            rep = eq[arm]
            for name, j in jobs.items():
                if not name.startswith(f"{row}_{rep}_s"):
                    continue
                seed = j["seed"]
                an = f"{row}_{eq['A']}_s{seed}"
                for gi, t in W.get(name, {}).items():
                    ta = W.get(an, {}).get(gi)
                    if ta is not None and t > ta:
                        out.append(dict(row=row, arm=arm, rep=rep, seed=seed, gi=gi, t=t, ta=ta, job=j, ajob=jobs[an]))
    return out


def single(j, gi, name):
    x = copy.deepcopy(j)
    x.update({"name": name, "games": 1, "seed": j["seed"] + gi, "game_index": gi})
    return x


def mk(ab, arms, od):
    os.makedirs(od, exist_ok=True)
    S = slower(ab, arms.split(","))
    d8, nc, idx = [], [], []
    seen = set()
    for s in S:
        tag = f"{s['row']}_{s['arm']}_s{s['seed']}_g{s['gi']}"
        x = single(s["job"], s["gi"], tag + "_d8")
        x.update({"depth": 8, "budget_ms": 0, "ignore_play_profile": True, "abandon_units": 40000000})
        d8.append(x)
        nc.append(single(s["job"], s["gi"], tag + "_nc"))
        ka = (s["row"], s["seed"], s["gi"])
        if ka not in seen:
            seen.add(ka)
            nc.append(single(s["ajob"], s["gi"], f"{s['row']}_A_s{s['seed']}_g{s['gi']}_nc"))
        idx.append(dict(tag=tag, row=s["row"], arm=s["arm"], seed=s["seed"], gi=s["gi"], t=s["t"], ta=s["ta"]))
    json.dump({"jobs": d8}, open(os.path.join(od, "d8.json"), "w"), indent=1)
    json.dump({"jobs": nc}, open(os.path.join(od, "nc.json"), "w"), indent=1)
    json.dump(idx, open(os.path.join(od, "index.json"), "w"), indent=1)
    print(f"{len(idx)} slower games -> {len(d8)} d8b0 jobs, {len(nc)} NC jobs in {od}")


def verdict(ab, od):
    idx = json.load(open(os.path.join(od, "index.json")))
    D8 = wins(os.path.join(od, "d8wins")) if os.path.isdir(os.path.join(od, "d8wins")) else {}
    NC = wins(os.path.join(od, "ncwins")) if os.path.isdir(os.path.join(od, "ncwins")) else {}
    tally = {}
    for e in idx:
        d8 = D8.get(e["tag"] + "_d8", {}).get(e["gi"])
        nca = NC.get(f"{e['row']}_A_s{e['seed']}_g{e['gi']}_nc", {}).get(e["gi"])
        ncv = NC.get(e["tag"] + "_nc", {}).get(e["gi"])
        if d8 is not None and d8 <= e["ta"]:
            v = "recover"
        elif nca is not None and ncv is not None and ncv <= nca:
            v = "clairvoyant"
        elif d8 is None or nca is None or ncv is None:
            v = "pending"
        else:
            v = "REAL"
        tally.setdefault((e["arm"], e["row"]), {}).setdefault(v, 0)
        tally[(e["arm"], e["row"])][v] += 1
        print(f"{e['arm']:6s} {e['row']:24s} seed {e['seed']} gi {e['gi']:4d}: A {e['ta']} -> {e['t']}  d8b0 {d8}  "
              f"NC A {nca} arm {ncv}  => {v}")
    print("tally:")
    for (arm, row), t in sorted(tally.items()):
        print(f"  {arm:6s} {row:24s} " + " ".join(f"{k}={v}" for k, v in sorted(t.items())))


if __name__ == "__main__":
    if sys.argv[1] == "mk":
        mk(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        verdict(sys.argv[2], sys.argv[3])
