#!/usr/bin/env python3
"""Value-leaf adoption IN THE REAL (budgeted) regime the regression/agents use.

test/regression_cases.sh runs a virtual-ms search budget: d3=10, d5=20. Whether the budget matters
depends on whether the search HITS it (vs converging first). Per deck/depth this prints:
  H_inf  = heuristic, UNBUDGETED (full depth reached)
  H_bud  = heuristic, at the regression budget (d3=10 / d5=20)
  V_bud  = value-leaf, at the regression budget
Reads: H_inf vs H_bud => does the budget BIND for the heuristic (LP/ms differ)?  V_bud vs H_bud =>
the actual adoption delta. value-leaf's rollout-free O(1) leaf spends less budget per leaf, so within a
fixed virtual-ms (node) budget it can explore MORE of the tree -> match/beat heuristic. LP=loss-penalised
avg win turn (losses=max_turns+1); ms/game is wall-clock. See learned-d0-policy.md.

    scripts/valueleaf_budget.py --games 250 --seed 2002 --threads 6
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
BUDGET = {3: 10, 5: 20}   # regression virtual-ms budgets


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
    return lp, 1000.0*dt/p, w, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=250)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--depths", nargs="+", type=int, default=[3, 5])
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_budget.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== VALUE-LEAF (BUDGETED regime)  games=%d seed=%d threads=%d =====" % (
        args.games, args.seed, args.threads))
    emit("%-8s %2s %4s   %8s %7s   %8s %7s   %8s %7s   %-14s %s" % (
        "deck","d","bud", "H_inf LP","ms", "H_bud LP","ms", "V_bud LP","ms",
        "budget binds?","V vs H_bud"))
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        for depth in args.depths:
            b = BUDGET.get(depth, 0)
            try:
                hi_lp, hi_ms, _, _ = run(deck, depth, args.games, args.seed, mt, args.threads, None, 0)
                hb_lp, hb_ms, _, _ = run(deck, depth, args.games, args.seed, mt, args.threads, None, b)
                vb_lp, vb_ms, vw, vp = run(deck, depth, args.games, args.seed, mt, args.threads, prof, b)
                binds = "YES d%+0.3f" % (hb_lp - hi_lp) if abs(hb_lp - hi_lp) > 1e-6 else "no (slack)"
                emit("%-8s d%d %4d   %8.3f %7.1f   %8.3f %7.1f   %8.3f %7.1f   %-14s %+7.3f (%dx)" % (
                    dname, depth, b, hi_lp, hi_ms, hb_lp, hb_ms, vb_lp, vb_ms, binds,
                    vb_lp - hb_lp, round(hb_ms/vb_ms) if vb_ms > 0 else 0))
            except Exception as e:
                emit("%-8s d%d  ERROR %s" % (dname, depth, e))
    of.close()


if __name__ == "__main__":
    main()
