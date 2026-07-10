#!/usr/bin/env python3
"""Budget HEADROOM: can we bump value-leaf's (node) budget to recover the leaf-approximation quality
cost while staying wall-clock cheaper than the heuristic@gate?

Under a node budget, value-leaf's cost is a worse leaf estimate at equal breadth; MORE budget = deeper
effective search = quality recovers (like the depth sweep). value-leaf is also cheaper per node (variable
by deck), so it may reach heuristic quality at a budget whose WALL-CLOCK is still <= heuristic@gate.
Per deck (d5, gate budget 20) prints heuristic@20 as the reference, then value-leaf at rising budgets;
the win is a value-leaf row with LP <= H and ms <= H_ms. LP=loss-penalised avg win turn. See
learned-d0-policy.md.

    scripts/valueleaf_headroom.py --games 250 --seed 2002 --threads 6 --depth 5
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
GATE = {3: 10, 5: 20}


def run(deck, depth, games, seed, mt, threads, profile, budget):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_NC_SEARCH"):
        env.pop(k, None)
    if profile:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
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
    ap.add_argument("--mults", nargs="+", type=int, default=[1, 2, 4, 8, 16])
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_headroom.txt")
    args = ap.parse_args()
    gate = GATE.get(args.depth, 20)
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== VALUE-LEAF BUDGET HEADROOM  d%d gate=%d  games=%d seed=%d =====" % (
        args.depth, gate, args.games, args.seed))
    emit("%-8s %-16s %8s %8s   %s" % ("deck","config","LP","ms","vs heur@gate"))
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        h_lp, h_ms = run(deck, args.depth, args.games, args.seed, mt, args.threads, None, gate)
        emit("%-8s %-16s %8.3f %8.1f   %s" % (dname, "heur@%d" % gate, h_lp, h_ms, "(reference)"))
        for mlt in args.mults:
            b = gate * mlt
            v_lp, v_ms = run(deck, args.depth, args.games, args.seed, mt, args.threads, prof, b)
            tag = "dLP=%+.3f  %.2fx wall" % (v_lp - h_lp, h_ms / v_ms if v_ms > 0 else 0)
            win = "  <== WIN (>= quality, faster)" if (v_lp <= h_lp + 1e-9 and v_ms <= h_ms) else ""
            emit("%-8s %-16s %8.3f %8.1f   %s%s" % (dname, "vleaf@%d(%dx)" % (b, mlt), v_lp, v_ms, tag, win))
    of.close()


if __name__ == "__main__":
    main()
