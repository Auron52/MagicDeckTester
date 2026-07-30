#!/usr/bin/env python3
"""Measure the NON-CLAIRVOYANT policy CEILING (reshuffle-averaged search) + performance.

For each deck: reference policies (heuristic d1, heuristic/value-leaf d5 = the clairvoyant search,
the best d0 model) and the NC ceiling grid over (K reshuffles, depth lookahead). Records quality
(avg win turn; loss-penalized LP with losses=max_turns+1) AND wall-clock per game. Writes
incrementally so partial results survive. See learned-d0-policy.md.

    scripts/nc_ceiling.py --games 100 --seeds 2002 3003 --threads 12 --out logs/eval/nc_ceiling_results.txt
"""
import argparse, os, re, subprocess, sys, time

MTG = "build/Release/mtg"

DECKS = {
    "antilife": ("decks/Anti-Lifegain.cod",  "decks/Anti-Lifegain.value.json",  "logs/eval/antilife_d0_qmodel_v2.eval.json"),
    "slivers":  ("decks/slivers_vial.txt",   "decks/slivers_vial.value.json",   "logs/eval/slivers_honest_gbdt.eval.json"),
    "TH":       ("decks/treasure_hunt.txt",  "decks/treasure_hunt.value.json",  "decks/treasure_hunt.eval.json"),
    "burn":     ("decks/burn.txt",           "decks/burn.value.json",           "logs/eval/burn_d0_anchor.eval.json"),
    "knights":  ("decks/Knights.cod",        "decks/Knights.value.json",        "logs/eval/knights_d0_gbdt.eval.json"),
}


def run(deck, depth, games, seed, mt, threads, env_extra):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_HONEST_PLAY","MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH",
              "MTG_DUMP_EVAL_ROWS","MTG_DUMP_VALUE_ROWS"):
        env.pop(k, None)
    env.update(env_extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed),
                          "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)],
                         capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    return p, w, a, dt


def agg(deck, depth, games, seeds, mt, threads, env_extra):
    tp=tw=tsum=tdt=0
    for s in seeds:
        p,w,a,dt = run(deck, depth, games, s, mt, threads, env_extra)
        tp+=p; tw+=w; tsum += w*a + (p-w)*(mt+1); tdt+=dt
    lp = tsum/tp
    awon = (tsum - (tp-tw)*(mt+1))/tw if tw else float("nan")
    return tw, tp, awon, lp, tdt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--seeds", nargs="+", type=int, default=[2002, 3003])
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=12)
    ap.add_argument("--decks", nargs="+", default=list(DECKS.keys()))
    ap.add_argument("--out", default="logs/eval/nc_ceiling_results.txt")
    args = ap.parse_args()

    of = open(args.out, "a")
    def emit(line):
        print(line); of.write(line+"\n"); of.flush()

    ng = args.games * len(args.seeds)
    emit("\n===== NC CEILING RUN  games=%d/seed seeds=%s threads=%d max_turns=%d =====" % (
        args.games, args.seeds, args.threads, args.max_turns))
    emit("%-9s %-24s %6s %6s %8s %8s %9s" % ("deck","policy","won","LP","avg_won","wall_s","ms/game"))

    for dname in args.decks:
        deck, valm, d0m = DECKS[dname]
        # reference policies
        refs = [
            ("heuristic d0", 0, {}),
            ("heuristic d1", 1, {}),
            ("heuristic d5 (search)", 5, {}),
            ("value-leaf d5 (search)", 5, {"MTG_VALUE_MODEL":"1","MTG_VALUE_PROFILE":valm}),
            ("d0-model", 0, {"MTG_EVAL_MODEL":"1","MTG_EVAL_PROFILE":d0m}),
        ]
        for name, depth, ee in refs:
            try:
                w,p,aw,lp,dt = agg(deck, depth, args.games, args.seeds, args.max_turns, args.threads, ee)
                emit("%-9s %-24s %4d/%-4d %6.3f %8.3f %8.1f %9.1f" % (
                    dname, name, w, p, lp, aw, dt, 1000.0*dt/p))
            except Exception as e:
                emit("%-9s %-24s  ERROR %s" % (dname, name, e))
        # NC ceiling grid
        cells = [("nc K8 d%d"%d, {"MTG_NC_SEARCH":"1","MTG_NC_K":"8","MTG_NC_DEPTH":str(d)}) for d in (0,1,2,3)]
        cells += [("nc K%d d2"%k, {"MTG_NC_SEARCH":"1","MTG_NC_K":str(k),"MTG_NC_DEPTH":"2"}) for k in (4,16)]
        for name, ee in cells:
            try:
                w,p,aw,lp,dt = agg(deck, 5, args.games, args.seeds, args.max_turns, args.threads, ee)
                emit("%-9s %-24s %4d/%-4d %6.3f %8.3f %8.1f %9.1f" % (
                    dname, name, w, p, lp, aw, dt, 1000.0*dt/p))
            except Exception as e:
                emit("%-9s %-24s  ERROR %s" % (dname, name, e))
    of.close()


if __name__ == "__main__":
    main()
