#!/usr/bin/env python3
"""Learning curve for the d0 land-fold RESULTING-STATE value model.

Answers "how much training data helps": trains the GBDT value model on increasing numbers of games
(subset the RS-value rows by seed) and measures the NON-CLAIRVOYANT land-fold play LP (MTG_D0LF_K=8)
on held-out seeds for each. Prints LP vs #games so the plateau is visible.

  scripts/rsvalue_learning_curve.py --rows logs/eval/TH_rsvalue_all.rows --deck decks/treasure_hunt.txt \
      --sizes 10 20 40 80 160 --base-seed 40000 --k 8
"""
import argparse, os, re, subprocess, tempfile

def train_and_measure(rows_all, deck, sizes, base_seed, held_seeds, k, mt, games):
    header = open(rows_all).readline()
    rows = [l for l in open(rows_all).read().splitlines()[1:] if l.strip()]
    by_seed = {}
    for l in rows:
        s = l.split()[-2]
        by_seed.setdefault(s, []).append(l)
    print(f"total games(seeds)={len(by_seed)}  candidates={len(rows)}")

    # heuristic reference
    print(f"{'games':>6} {'decisions':>10} {'cands':>7} {'trainRMSE':>10} {'NC-LP':>8}")
    def measure(model, extra_env):
        env = {kk: vv for kk, vv in os.environ.items() if not kk.startswith("MTG_")}
        env.update(extra_env)
        sw = sp = 0; lps = []
        for s in held_seeds:
            out = subprocess.run(["build/Release/mtg", deck, "--games", str(games), "--seed", str(s),
                                  "--depth", "0", "--max-turns", str(mt), "--threads", "6"],
                                 capture_output=True, text=True, env=env).stdout
            p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
            w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
            m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else 0.0
            lps.append((w*a+(p-w)*(mt+1))/p); sw += w; sp += p
        return sum(lps)/len(lps)

    heur = measure(None, {})
    print(f"{'heur':>6} {'-':>10} {'-':>7} {'-':>10} {heur:8.3f}")

    for nseeds in sizes:
        seeds = [str(base_seed + i) for i in range(nseeds)]
        sub = [l for s in seeds if s in by_seed for l in by_seed[s]]
        if not sub:
            continue
        ndec = len(set((l.split()[-2], l.split()[-1]) for l in sub))
        with tempfile.NamedTemporaryFile("w", suffix=".rows", delete=False) as tf:
            tf.write(header)
            tf.write("\n".join(sub) + "\n")
            subrows = tf.name
        model = f"/tmp/rsv_lc_{nseeds}.value.json"
        tr = subprocess.run(["python3", "scripts/train_eval_gbdt.py", "--rows", subrows,
                             "--regression", "--out", model, "--trees", "120", "--depth", "4"],
                            capture_output=True, text=True)
        rmse = "NA"
        m = re.findall(r"train_RMSE=([\d.]+)", tr.stdout + tr.stderr)
        if m:
            rmse = m[-1]
        lp = measure(model, {"MTG_D0_LANDFOLD": "1", "MTG_VALUE_PROFILE": model, "MTG_D0LF_K": str(k)})
        print(f"{nseeds:>6} {ndec:>10} {len(sub):>7} {rmse:>10} {lp:8.3f}", flush=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", required=True)
    ap.add_argument("--deck", required=True)
    ap.add_argument("--sizes", type=int, nargs="+", default=[10, 20, 40, 80, 160])
    ap.add_argument("--base-seed", type=int, default=40000)
    ap.add_argument("--held-seeds", type=int, nargs="+", default=[4001, 4002, 9009, 9010])
    ap.add_argument("--k", type=int, default=8)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--games", type=int, default=100)
    args = ap.parse_args()
    train_and_measure(args.rows, args.deck, args.sizes, args.base_seed, args.held_seeds,
                      args.k, args.max_turns, args.games)

if __name__ == "__main__":
    main()
