#!/usr/bin/env python3
"""How big is the NON-CLAIRVOYANT prize? Measure the fast d0 tier vs increasing-budget NC search
(the teacher a distilled fast non-clairvoyant model would reach for) -- quality (LP) + speed (ms/game).
Provider tempo bonus is ON by default (env unset). value-leaf d5 shown as the clairvoyant reference.

    scripts/nc_budget.py --games 40 --seed 2002 --threads 2 --decks antilife TH
"""
import argparse, os, re, subprocess, sys, time

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.value.json", "logs/eval/antilife_d0_qmodel_v2.eval.json"),
    "TH":       ("decks/treasure_hunt.txt", "decks/treasure_hunt.value.json", "decks/treasure_hunt.eval.json"),
    "slivers":  ("decks/slivers_vial.txt",  "decks/slivers_vial.value.json",  "logs/eval/slivers_honest_gbdt.eval.json"),
    "burn":     ("decks/burn.txt",           "decks/burn.value.json",          "logs/eval/burn_d0_anchor.eval.json"),
    "knights":  ("decks/Knights.cod",        "decks/Knights.value.json",       "logs/eval/knights_d0_gbdt.eval.json"),
}


def run(deck, depth, games, seed, mt, threads, extra):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH","MTG_NC_TEMPO","MTG_NC_TEMPO_LANDS"):
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
    return p, w, a, lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=40)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--decks", nargs="+", default=["antilife","TH"])
    ap.add_argument("--out", default="logs/eval/nc_budget.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s); of.write(s+"\n"); of.flush()
    emit("\n===== NC BUDGET (prize size)  games=%d seed=%d threads=%d mt=%d =====" % (
        args.games, args.seed, args.threads, args.max_turns))
    emit("%-9s %-16s %6s %8s %8s %10s" % ("deck","policy","won","avg_won","LP","ms/game"))
    for dname in args.decks:
        deck, valm, d0m = DECKS[dname]
        policies = [
            ("d0-model",      0, {"MTG_EVAL_MODEL":"1","MTG_EVAL_PROFILE":d0m}),
            ("value-leaf d5", 5, {"MTG_VALUE_MODEL":"1","MTG_VALUE_PROFILE":valm}),
            ("NC K8 d2",      5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"8","MTG_NC_DEPTH":"2"}),
            ("NC K16 d3",     5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"16","MTG_NC_DEPTH":"3"}),
            ("NC K32 d3",     5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"32","MTG_NC_DEPTH":"3"}),
        ]
        for name, depth, ee in policies:
            try:
                p,w,a,lp,mspg = run(deck, depth, args.games, args.seed, args.max_turns, args.threads, ee)
                emit("%-9s %-16s %4d/%-4d %8.3f %8.3f %10.1f" % (dname,name,w,p,a,lp,mspg))
            except Exception as e:
                emit("%-9s %-16s  ERROR %s" % (dname,name,e))
    of.close()


if __name__ == "__main__":
    main()
