#!/usr/bin/env python3
"""Paired A/B: per-depth table crossover (default) vs legacy uniform offset (MTG_VALUE_TRUST_OFFSET=3),
on HELD-OUT seeds (disjoint from calibration 8008/9009 AND regression 1001/2002/3003).

Paired: same deck/depth/seed/games -> most games are byte-identical between arms; only decisions where the
value-leaf committed at a depth whose per-depth hc* differs from the uniform 'committed-3' rule can flip.
So the avg-win-turn delta isolates the crossover's net bias with far less N than an unpaired test.

Reports LP (loss-penalised avg win turn, loss=max_turns+1) per arm and the paired delta (B=uniform is the
GT baseline; A=per-depth is the candidate). dLP = A-B; NEGATIVE => per-depth crossover is BETTER."""
import argparse, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import valueleaf_depth_matrix as vm

def run(deck_path, depth, seed, games, budget, threads, uniform_off):
    env = dict(os.environ)
    # normal hybrid play (value model auto-loaded from the deck .value.json); do NOT set MTG_VALUE_MODEL/etc.
    for k in ("MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_VALUE_MIN_DEPTH","MTG_VALUE_STARTGATE_ALPHA","MTG_VALUE_TRUST_OFFSET"):
        env.pop(k, None)
    if uniform_off is not None:
        env["MTG_VALUE_TRUST_OFFSET"] = str(uniform_off)   # force legacy uniform rule (ignore table crossover)
    cmd = [vm.MTG, deck_path, "--depth", str(depth), "--seed", str(seed), "--games", str(games),
           "--max-turns", "8", "--budget-ms", str(budget), "--threads", str(threads)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*9) / p
    return p, w, a, lp

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--seeds", type=int, nargs="+", default=[40000, 55000, 70000])
    ap.add_argument("--configs", nargs="+", default=["slivers:3:12000:10","slivers:5:8000:20",
                                                     "TH:3:12000:10","TH:5:8000:20"],
                    help="deck:depth:games:budget")
    a = ap.parse_args()
    print("PAIRED A/B  A=per-depth crossover (default)   B=uniform offset=3 (legacy/GT)   dLP=A-B (neg=crossover better)\n")
    for cfg in a.configs:
        deck, depth, games, budget = cfg.split(":"); depth=int(depth); games=int(games); budget=int(budget)
        deck_path = vm.DECKS[deck][0]
        tot = {"A":[0,0,0.0], "B":[0,0,0.0]}   # played, won, weighted-lp-sum
        print(f"===== {deck} d{depth}  games/seed={games} budget={budget}ms  seeds={a.seeds} =====")
        for seed in a.seeds:
            pA,wA,avA,lpA = run(deck_path, depth, seed, games, budget, a.threads, None)
            pB,wB,avB,lpB = run(deck_path, depth, seed, games, budget, a.threads, 3)
            print(f"  seed {seed:6d}: A won={wA}/{pA} avg={avA:.5f} LP={lpA:.5f} | "
                  f"B won={wB}/{pB} avg={avB:.5f} LP={lpB:.5f} | dLP={lpA-lpB:+.5f} (dWon={wA-wB:+d})")
            for arm,(p,w,lp) in (("A",(pA,wA,lpA)),("B",(pB,wB,lpB))):
                tot[arm][0]+=p; tot[arm][1]+=w; tot[arm][2]+=lp*p
        lpA = tot["A"][2]/tot["A"][0]; lpB = tot["B"][2]/tot["B"][0]
        print(f"  >>> AGG {deck} d{depth}: A LP={lpA:.5f} (won {tot['A'][1]})  B LP={lpB:.5f} (won {tot['B'][1]})  "
              f"dLP={lpA-lpB:+.5f}  dWon={tot['A'][1]-tot['B'][1]:+d}  over {tot['A'][0]} games\n")

if __name__ == "__main__":
    main()
