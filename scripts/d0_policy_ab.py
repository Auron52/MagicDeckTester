#!/usr/bin/env python3
"""d0 PLAY policy vs the NC teacher — the gap a d0 replacement must close.

The primary goal is a d0-speed policy (one model call per decision, NO search tree). This measures the
loss-penalised avg win turn (LP, losses=max_turns+1) of:
  - heuristic d0            (the hand-tuned baseline)
  - eval-model d0           (a shipped/candidate <deck>.eval.json served at depth 0)
  - NC teacher (K16 d2)     (the validated non-clairvoyant target)
across several held-out seeds (mean LP; single-seed swing ~0.3 dwarfs the effect).

    scripts/d0_policy_ab.py --decks TH antilife --models TH=logs/eval/TH_ncpolicy_d0.eval.json ...
"""
import argparse, os, re, subprocess, time
from collections import defaultdict

MTG = "build/Release/mtg"
DECKS = {
    "TH":       ("decks/treasure_hunt.txt", 8),
    "antilife": ("decks/Anti-Lifegain.cod", 8),
    "slivers":  ("decks/slivers_vial.txt",  8),
    "burn":     ("decks/burn.txt",           8),
    "knights":  ("decks/Knights.cod",        8),
}
CLEAR = ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
         "MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH","MTG_NC_TEMPO","MTG_NC_TEMPO_LANDS")


def run(deck, depth, games, seed, mt, threads, extra):
    env = dict(os.environ)
    for k in CLEAR:
        env.pop(k, None)
    env.update(extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed),
                          "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)],
                         capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(mt+1)) / p
    return lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--seeds", type=int, nargs="+", default=[2002, 3003, 7007])
    ap.add_argument("--decks", nargs="+", default=["TH", "antilife"])
    ap.add_argument("--models", nargs="+", default=[], help="deck=path.eval.json for the d0 candidate")
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--out", default="logs/eval/d0_policy_ab.txt")
    args = ap.parse_args()
    models = dict(m.split("=", 1) for m in args.models)
    of = open(args.out, "a")
    def emit(s):
        print(s); of.write(s + "\n"); of.flush()
    emit("\n===== d0 POLICY A/B  games=%d seeds=%s =====" % (args.games, args.seeds))
    emit("%-9s %-16s %8s %10s" % ("deck", "policy", "meanLP", "ms/game"))
    for dname in args.decks:
        deck, mt = DECKS[dname]
        policies = [("heuristic d0", 0, {})]
        if dname in models:
            policies.append(("ncpolicy d0", 0, {"MTG_EVAL_MODEL": "1", "MTG_EVAL_PROFILE": models[dname]}))
        shipped = f"decks/{ {'TH':'treasure_hunt','antilife':'Anti-Lifegain','slivers':'slivers_vial','burn':'burn','knights':'Knights'}[dname] }.eval.json"
        if os.path.exists(shipped):
            policies.append(("shipped-eval d0", 0, {"MTG_EVAL_MODEL": "1", "MTG_EVAL_PROFILE": shipped}))
        policies.append(("NC teacher K16d2", 5, {"MTG_NC_SEARCH": "1", "MTG_NC_K": "16", "MTG_NC_DEPTH": "2"}))
        for name, depth, ee in policies:
            lps, mss = [], []
            for seed in args.seeds:
                lp, ms = run(deck, depth, args.games, seed, mt, args.threads, ee)
                lps.append(lp); mss.append(ms)
            emit("%-9s %-16s %8.3f %10.1f  (%s)" % (
                dname, name, sum(lps)/len(lps), sum(mss)/len(mss),
                " ".join("%.3f" % x for x in lps)))
    of.close()


if __name__ == "__main__":
    main()
