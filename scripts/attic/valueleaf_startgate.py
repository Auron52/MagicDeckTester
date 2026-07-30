#!/usr/bin/env python3
"""Start-gate-alpha sweep for the value-leaf HYBRID (MTG_VALUE_MIN_DEPTH=5, redo=heuristic) at the budgeted
regime. MTG_VALUE_STARTGATE_ALPHA relaxes the iterative-deepening start gate WHEN the value-leaf is active,
so a nearly-affordable transitional pass FINISHES within the original search (reaching the trust depth K)
instead of committing short and triggering the separate (expensive) heuristic redo. A genuinely-explosive
pass (estimate many x remaining, e.g. slivers g4) is still rejected -> no blowup.

Compares baseline hybrid (alpha=1.0=off) vs alpha in {2,3,5}. Win = LP <= baseline (no quality loss) AND
ms <= baseline (fewer redos => faster). LP = loss-penalised avg win turn. See docs/design/learned-d0-policy.md.

    scripts/valueleaf_startgate.py --games 250 --seed 2002 --threads 6 --budget 20 --depth 5
NOTE: ms is wall-clock and noisy under concurrent load; configs run back-to-back so contention is shared.
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


def run(deck, depth, games, seed, mt, threads, profile, budget, min_depth, alpha):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
    env["MTG_VALUE_MIN_DEPTH"] = str(min_depth)
    if alpha and alpha != 1.0:
        env["MTG_VALUE_STARTGATE_ALPHA"] = str(alpha)
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
    return lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=250)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--budget", type=int, default=20)
    ap.add_argument("--min-depth", type=int, default=5)
    ap.add_argument("--alphas", nargs="+", type=float, default=[2.0, 3.0, 5.0])
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_startgate.txt")
    args = ap.parse_args()
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== START-GATE ALPHA SWEEP (K=%d, d%d budget=%d)  games=%d seed=%d =====" % (
        args.min_depth, args.depth, args.budget, args.games, args.seed))
    hdr = "%-8s   %8s %6s" % ("deck", "base LP","ms")
    for a in args.alphas: hdr += "   %6s %6s" % ("a%g LP"%a, "ms")
    emit(hdr)
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        try:
            b_lp, b_ms = run(deck, args.depth, args.games, args.seed, mt, args.threads, prof, args.budget,
                             args.min_depth, 1.0)
            row = "%-8s   %8.3f %6.1f" % (dname, b_lp, b_ms)
            best = None
            for a in args.alphas:
                lp, ms = run(deck, args.depth, args.games, args.seed, mt, args.threads, prof, args.budget,
                             args.min_depth, a)
                row += "   %6.3f %6.1f" % (lp, ms)
                win = (lp <= b_lp + 1e-9 and ms <= b_ms + 1e-9)
                if win and (best is None or ms < best[1]): best = (a, ms, lp)
            if best: row += "   BEST a%g %.2fx dLP=%+.3f" % (best[0], b_ms/best[1], best[2]-b_lp)
            emit(row)
        except Exception as e:
            emit("%-8s  ERROR %s" % (dname, e))
    of.close()


if __name__ == "__main__":
    main()
