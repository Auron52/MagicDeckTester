#!/usr/bin/env python3
"""Adaptive leaf gate (MTG_VALUE_MIN_DEPTH=5) in the REAL budgeted regime: does gating value-leaf to
DEEP passes (>=5 plies) remove the shallow-leaf quality cost while keeping the speedup?

Per deck at the regression budgets (d3=10, d5=20) compares:
  H      = heuristic rollout leaf (reference)
  V0     = value-leaf, ungated (MTG_VALUE_MIN_DEPTH unset) -- the +0.05 antilife budgeted regression
  V5     = value-leaf, gated to pass depth >= 5 (adaptive: heuristic on shallow passes, value-leaf deep)
Win = V5 LP <= H (no quality loss) AND V5 ms <= H ms (still faster). At d3 the gate should make V5 == H
(no pass reaches 5 -> pure heuristic). LP = loss-penalised avg win turn. See learned-d0-policy.md.

    scripts/valueleaf_adaptive.py --games 250 --seed 2002 --threads 6
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


def run(deck, depth, games, seed, mt, threads, profile, budget, min_depth=None):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH"):
        env.pop(k, None)
    if profile:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
    if min_depth is not None:
        env["MTG_VALUE_MIN_DEPTH"] = str(min_depth)
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
    ap.add_argument("--min-depth", type=int, default=5)
    ap.add_argument("--depths", nargs="+", type=int, default=[3, 5])
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_adaptive.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== ADAPTIVE LEAF GATE (K=%d, budgeted)  games=%d seed=%d =====" % (
        args.min_depth, args.games, args.seed))
    emit("%-8s %2s %4s   %8s %6s   %8s %6s   %8s %6s   %s" % (
        "deck","d","bud", "H LP","ms", "V0 LP","ms", "V5 LP","ms", "V5 vs H"))
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        for depth in args.depths:
            b = GATE.get(depth, 0)
            try:
                h_lp, h_ms   = run(deck, depth, args.games, args.seed, mt, args.threads, None, b)
                v0_lp, v0_ms = run(deck, depth, args.games, args.seed, mt, args.threads, prof, b)
                v5_lp, v5_ms = run(deck, depth, args.games, args.seed, mt, args.threads, prof, b, args.min_depth)
                tag = "dLP=%+.3f %.2fx" % (v5_lp - h_lp, h_ms/v5_ms if v5_ms>0 else 0)
                if v5_lp <= h_lp + 1e-9 and v5_ms <= h_ms + 1e-9: tag += "  <== WIN"
                emit("%-8s d%d %4d   %8.3f %6.1f   %8.3f %6.1f   %8.3f %6.1f   %s" % (
                    dname, depth, b, h_lp, h_ms, v0_lp, v0_ms, v5_lp, v5_ms, tag))
            except Exception as e:
                emit("%-8s d%d  ERROR %s" % (dname, depth, e))
    of.close()


if __name__ == "__main__":
    main()
