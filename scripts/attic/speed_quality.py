#!/usr/bin/env python3
"""Speed vs quality of the FAST MODELS vs the search -- the 'model in practice' benchmark.

Per deck, runs each policy over many autonomous games and reports quality (avg win turn; loss-penalised
LP, losses=max_turns+1) AND speed (ms/game). Policies:
  heuristic d0     -- greedy, no search, no model (fastest heuristic floor)
  d0-model         -- learned O(1) plan ranker, NON-clairvoyant, no search (the fast model)
  heuristic d1     -- 1-ply search
  heuristic d5     -- deep CLAIRVOYANT search (quality ceiling, slow)
  value-leaf d5    -- deep search with a learned O(1) leaf (clairvoyant-search quality, faster)
  NC+tempo K8 d2   -- reshuffle-averaged NON-clairvoyant search + the provider tempo bonus (best NC play, slow)

    scripts/speed_quality.py --games 120 --seed 2002 --threads 6
"""
import argparse, os, re, subprocess, sys, time

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.value.json", "logs/eval/antilife_d0_qmodel_v2.eval.json"),
    "slivers":  ("decks/slivers_vial.txt",  "decks/slivers_vial.value.json",  "logs/eval/slivers_honest_gbdt.eval.json"),
    "TH":       ("decks/treasure_hunt.txt", "decks/treasure_hunt.value.json", "decks/treasure_hunt.eval.json"),
    "burn":     ("decks/burn.txt",          "decks/burn.value.json",          "logs/eval/burn_d0_anchor.eval.json"),
    "knights":  ("decks/Knights.cod",       "decks/Knights.value.json",       "logs/eval/knights_d0_gbdt.eval.json"),
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
    ap.add_argument("--games", type=int, default=120)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/speed_quality.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s); of.write(s+"\n"); of.flush()
    emit("\n===== SPEED vs QUALITY  games=%d seed=%d threads=%d mt=%d =====" % (
        args.games, args.seed, args.threads, args.max_turns))
    emit("%-9s %-16s %6s %8s %8s %10s" % ("deck","policy","won","avg_won","LP","ms/game"))
    for dname in args.decks:
        deck, valm, d0m = DECKS[dname]
        policies = [
            ("heuristic d0", 0, {}),
            ("d0-model",     0, {"MTG_EVAL_MODEL":"1","MTG_EVAL_PROFILE":d0m}),
            ("heuristic d1", 1, {}),
            ("heuristic d5", 5, {}),
            ("value-leaf d5",5, {"MTG_VALUE_MODEL":"1","MTG_VALUE_PROFILE":valm}),
            ("NC+tempo K8d2",5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"8","MTG_NC_DEPTH":"2"}),  # provider tempo (env unset)
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
