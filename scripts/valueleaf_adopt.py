#!/usr/bin/env python3
"""Adoption measurement: value-leaf (learned O(1) search leaf) vs the heuristic greedy leaf, per deck,
at the depths where the search LEAF matters (d3, d5). Reports quality (avg win turn; loss-penalised LP,
losses=max_turns+1) and speed (ms/game) + the speedup and LP delta. value-leaf replaces the rollout leaf,
so it is NOT byte-identical to the heuristic (plan rankings shift) -- adoption needs a GT rebaseline; this
measures whether the quality is preserved (LP delta ~0) at a large speedup, the go/no-go for default-on.

    scripts/valueleaf_adopt.py --games 250 --seed 2002 --threads 6
"""
import argparse, os, re, subprocess, time

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.value.json", 8),
    "slivers":  ("decks/slivers_vial.txt",  "decks/slivers_vial.value.json",  8),
    "TH":       ("decks/treasure_hunt.txt", "decks/treasure_hunt.value.json", 8),
    "burn":     ("decks/burn.txt",          "decks/burn.value.json",          8),
    "knights":  ("decks/Knights.cod",       "decks/Knights.value.json",       8),
}


# Regression per-decision virtual-ms budgets (test/regression_cases.sh: d3=10, d5=20). 0 = unlimited.
BUDGETS = {3: 10, 5: 20}


def run(deck, depth, games, seed, mt, threads, profile=None, budget=None):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_NC_SEARCH"):
        env.pop(k, None)
    if profile:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
    if budget is None:
        budget = BUDGETS.get(depth, 0)
    cmd = [MTG, deck, "--games", str(games), "--seed", str(seed),
           "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)]
    if budget and budget > 0:
        cmd += ["--budget-ms", str(budget)]
    t0 = time.time()
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(mt+1)) / p
    return p, w, a, lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=250)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--depths", nargs="+", type=int, default=[3, 5])
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_adopt.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== VALUE-LEAF ADOPTION  games=%d seed=%d threads=%d =====" % (args.games, args.seed, args.threads))
    emit("%-8s %2s  %-9s %8s %8s %9s   %-9s %8s %8s %9s   %7s %7s" % (
        "deck","d","heur LP","h_avg","h_ms","", "vleaf LP","v_avg","v_ms","", "dLP","speedup"))
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        for depth in args.depths:
            try:
                hp,hw,ha,hlp,hms = run(deck, depth, args.games, args.seed, mt, args.threads, None)
                vp,vw,va,vlp,vms = run(deck, depth, args.games, args.seed, mt, args.threads, prof)
                emit("%-8s d%d  %8.3f %8.3f %8.1f %9s %8.3f %8.3f %8.1f %9s   %+6.3f %6.1fx" % (
                    dname, depth, hlp, ha, hms, "(%d/%d)"%(hw,hp),
                    vlp, va, vms, "(%d/%d)"%(vw,vp), vlp-hlp, hms/vms if vms>0 else 0))
            except Exception as e:
                emit("%-8s d%d  ERROR %s" % (dname, depth, e))
    of.close()


if __name__ == "__main__":
    main()
