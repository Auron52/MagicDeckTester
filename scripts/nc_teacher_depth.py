#!/usr/bin/env python3
"""Pin the NC TEACHER depth: does the reshuffle-avg search keep improving past d2?

n=60 single-seed reads are unreliable (seed-to-seed LP swing ~0.3 dwarfs the depth
effect), so we sweep an explicit (deck, K, depth) grid across SEVERAL seeds and print
the per-seed cells plus the cross-seed MEAN LP per config. The mean is what sets the
teacher depth for distillation. Deep/wide cells are slow (NC branching is B^depth x K),
so each run has a wall-clock guard and configs are given explicitly to control cost.

    scripts/nc_teacher_depth.py --decks TH antilife --ks 8 16 --depths 1 2 3 \
        --seeds 2002 3003 7007 --games 60 --threads 4
"""
import argparse, os, re, subprocess, time
from collections import defaultdict

MTG = "build/Release/mtg"
DECKS = {
    "antilife": "decks/Anti-Lifegain.cod",
    "TH":       "decks/treasure_hunt.txt",
    "slivers":  "decks/slivers_vial.txt",
    "burn":     "decks/burn.txt",
    "knights":  "decks/Knights.cod",
}
CLEAR = ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
         "MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH","MTG_NC_TEMPO","MTG_NC_TEMPO_LANDS")


def run(deck, K, depth, games, seed, mt, threads, guard):
    env = dict(os.environ)
    for k in CLEAR:
        env.pop(k, None)
    env.update({"MTG_NC_SEARCH": "1", "MTG_NC_K": str(K), "MTG_NC_DEPTH": str(depth)})
    t0 = time.time()
    # NC continuation uses `depth` internally; the engine --depth also passes 5 (>= any NC depth).
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed),
                          "--depth", "5", "--max-turns", str(mt), "--threads", str(threads)],
                         capture_output=True, text=True, env=env, timeout=guard).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(mt+1)) / p
    return p, w, a, lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=60)
    ap.add_argument("--seeds", type=int, nargs="+", default=[2002, 3003, 7007])
    ap.add_argument("--decks", nargs="+", default=["TH", "antilife"])
    ap.add_argument("--ks", type=int, nargs="+", default=[8])
    ap.add_argument("--depths", type=int, nargs="+", default=[1, 2, 3])
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--guard", type=int, default=1200, help="per-run wall-clock cap (s)")
    ap.add_argument("--out", default="logs/eval/nc_teacher_depth.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s); of.write(s + "\n"); of.flush()
    emit("\n===== NC TEACHER DEPTH  games=%d seeds=%s threads=%d mt=%d =====" % (
        args.games, args.seeds, args.threads, args.max_turns))
    emit("%-9s %-9s %8s %8s %10s %6s" % ("deck", "config", "LP", "avg_won", "ms/game", "seed"))
    agg = defaultdict(list)   # (deck, config) -> [lp, ...]
    for dname in args.decks:
        deck = DECKS[dname]
        for K in args.ks:
            for d in args.depths:
                cfg = "K%d d%d" % (K, d)
                for seed in args.seeds:
                    try:
                        p, w, a, lp, mspg = run(deck, K, d, args.games, seed,
                                                args.max_turns, args.threads, args.guard)
                        emit("%-9s %-9s %8.3f %8.3f %10.1f %6d" % (dname, cfg, lp, a, mspg, seed))
                        agg[(dname, cfg)].append(lp)
                    except subprocess.TimeoutExpired:
                        emit("%-9s %-9s   TIMEOUT (>%ds) seed=%d" % (dname, cfg, args.guard, seed))
                    except Exception as e:
                        emit("%-9s %-9s   ERROR %s seed=%d" % (dname, cfg, e, seed))
    emit("----- cross-seed MEAN LP -----")
    for (dname, cfg), lps in agg.items():
        emit("%-9s %-9s mean_LP=%.3f  n_seeds=%d  (%s)" % (
            dname, cfg, sum(lps)/len(lps), len(lps), " ".join("%.3f" % x for x in lps)))
    of.close()


if __name__ == "__main__":
    main()
