#!/usr/bin/env python3
"""Measure d0 PLAY LP for the dynamic model vs heuristic vs NC teacher.

The dynamic (latent-rollout) model is served at Seam A via MTG_DYN_MODEL and plays at --depth 0
(one model call per plan-rank, no search tree). This is the goal-#1 non-clairvoyant d0 policy.
LP = loss-penalised avg win turn (losses = max_turns+1). Single-seed swing ~0.3, so aggregate seeds.

  scripts/dyn_ab.py --deck TH --games 100 --seeds 2002 3003 7007 \
      --dyn best=logs/eval/TH_dyn_v2.json --dyn v3=logs/eval/TH_dyn_v3.json --teacher
"""
import argparse, os, re, subprocess, time

MTG = "build/Release/mtg"
DECKS = {  # name -> (path, max_turns)
    "TH":       ("decks/treasure_hunt.txt", 8),
    "antilife": ("decks/Anti-Lifegain.cod", 8),
    "slivers":  ("decks/slivers_vial.txt",  8),
    "burn":     ("decks/burn.txt",          8),
    "knights":  ("decks/Knights.cod",       8),
}
CLEAR = ["MTG_DYN_MODEL", "MTG_EVAL_MODEL", "MTG_EVAL_PROFILE", "MTG_VALUE_MODEL",
         "MTG_VALUE_PROFILE", "MTG_NC_SEARCH", "MTG_NC_K", "MTG_NC_DEPTH", "MTG_NC_TEMPO"]


def run(deck, mt, depth, games, seed, threads, extra):
    env = {k: v for k, v in os.environ.items() if k not in CLEAR}
    env.update(extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed),
                          "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)],
                         capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else 0.0
    lp = (w * a + (p - w) * (mt + 1)) / p
    return w, p, lp, dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True)
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--seeds", type=int, nargs="+", default=[2002, 3003, 7007])
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--dyn", action="append", default=[], help="label=path.json (served at depth 0)")
    ap.add_argument("--teacher", action="store_true", help="also run NC K16 d2 (the target)")
    ap.add_argument("--nc-k", type=int, default=16)
    ap.add_argument("--nc-depth", type=int, default=2)
    ap.add_argument("--out", default="logs/eval/dyn_ab.txt")
    args = ap.parse_args()
    deck, mt = DECKS[args.deck]
    of = open(args.out, "a")

    def emit(s):
        print(s); of.write(s + "\n"); of.flush()

    emit("\n===== dyn A/B  deck=%s games=%d seeds=%s =====" % (args.deck, args.games, args.seeds))
    policies = [("heuristic d0", 0, {})]
    for spec in args.dyn:
        lbl, path = spec.split("=", 1)
        policies.append(("dyn:" + lbl, 0, {"MTG_DYN_MODEL": path}))
    if args.teacher:
        policies.append(("NC K%dd%d" % (args.nc_k, args.nc_depth), 5,
                         {"MTG_NC_SEARCH": "1", "MTG_NC_K": str(args.nc_k), "MTG_NC_DEPTH": str(args.nc_depth)}))
    for name, depth, extra in policies:
        lps, sw, sp, tt = [], 0, 0, 0.0
        per = []
        for seed in args.seeds:
            w, p, lp, dt = run(deck, mt, depth, args.games, seed, args.threads, extra)
            lps.append(lp); sw += w; sp += p; tt += dt; per.append("%.3f" % lp)
        emit("  %-18s win%%=%5.1f  meanLP=%.3f  [%s]  %.1fs/seed" % (
            name, 100.0 * sw / sp, sum(lps) / len(lps), " ".join(per), tt / len(args.seeds)))
    of.close()


if __name__ == "__main__":
    main()
