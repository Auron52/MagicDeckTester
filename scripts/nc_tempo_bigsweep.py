#!/usr/bin/env python3
"""Broad AUTONOMOUS validation of the NC land-drop tempo bonus (MTG_NC_TEMPO / MTG_NC_TEMPO_LANDS).

Unlike ref_bench (30 hand-played reference hands), this runs the goldfish runner's own mulligan +
play over MANY fresh seeds per deck -- the general no-regression gate before turning the bonus on.
For each deck x config it reports games won + avg win turn + loss-penalised LP (losses=max_turns+1).
Seeds are disjoint from the references (1-31) and the regression suite. NC search is ~3 GB/game, so
the binary runs at low internal --threads; decks/configs run sequentially. Writes incrementally.

    scripts/nc_tempo_bigsweep.py --games 100 --seed 40000 --threads 2 --out logs/eval/nc_tempo_bigsweep.txt
"""
import argparse, os, re, subprocess, sys, time

MTG = "build/Release/mtg"
DECKS = {
    "antilife": "decks/Anti-Lifegain.cod",
    "burn":     "decks/burn.txt",
    "slivers":  "decks/slivers_vial.txt",
    "knights":  "decks/Knights.cod",
    "TH":       "decks/treasure_hunt.txt",
}
# (label, extra NC env). All share MTG_NC_SEARCH/K/DEPTH; TEMPO=0 is the current byte-identical NC.
CONFIGS = [
    ("baseline",  {"MTG_NC_TEMPO":"0"}),   # true no-bonus control (env set to 0)
    ("provider",  {}),                      # ADOPTED: env unset -> archetype provider decides
    ("t1.0_L2",   {"MTG_NC_TEMPO":"1.0","MTG_NC_TEMPO_LANDS":"2"}),   # ref: global gated
    ("t1.0_L99",  {"MTG_NC_TEMPO":"1.0","MTG_NC_TEMPO_LANDS":"99"}),  # ref: global ungated
]


def run(deck_file, games, seed, mt, threads, extra):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_VALUE_MODEL","MTG_NC_TEMPO","MTG_NC_TEMPO_LANDS"):
        env.pop(k, None)
    env["MTG_NC_SEARCH"]="1"; env["MTG_NC_K"]="8"; env["MTG_NC_DEPTH"]="2"
    env.update(extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck_file, "--games", str(games), "--seed", str(seed),
                          "--depth", "5", "--max-turns", str(mt), "--threads", str(threads)],
                         capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(mt+1)) / p
    return p, w, a, lp, dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--seed", type=int, default=40000)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/nc_tempo_bigsweep.txt")
    args = ap.parse_args()

    of = open(args.out, "a")
    def emit(s):
        print(s); of.write(s+"\n"); of.flush()
    emit("\n===== NC TEMPO BIGSWEEP games=%d seed=%d threads=%d mt=%d =====" % (
        args.games, args.seed, args.threads, args.max_turns))
    emit("%-9s %-10s %8s %8s %8s %8s" % ("deck","config","won","avg_won","LP","wall_s"))
    for dname in args.decks:
        deck_file = DECKS[dname]
        base = {}
        for label, extra in CONFIGS:
            try:
                p,w,a,lp,dt = run(deck_file, args.games, args.seed, args.max_turns, args.threads, extra)
            except Exception as e:
                emit("%-9s %-10s  ERROR %s" % (dname, label, e)); continue
            if label == "baseline": base = {"lp":lp,"w":w}
            dLP = "" if label=="baseline" else "  dLP=%+.3f dWon=%+d" % (lp-base.get("lp",lp), w-base.get("w",w))
            emit("%-9s %-10s %4d/%-4d %8.3f %8.3f %8.1f%s" % (dname,label,w,p,a,lp,dt,dLP))
    of.close()


if __name__ == "__main__":
    main()
