#!/usr/bin/env python3
"""Q1: is the learned d0 SCORER faster AND >= quality than the d0 HEURISTIC ROLLOUT?

Both are non-clairvoyant d0 policies that, for each candidate plan, average over K reshuffles. They differ
ONLY in the leaf:
  - NC-d0 (heuristic rollout): greedy-play each reshuffle to the END (O(turns)/candidate)  -- the thing to beat
  - land-fold DYN d0 (model) : evaluate the RESULTING state with the value net (O(1)/candidate)
If the net keeps quality but skips the rollout it should be strictly faster -> an adoptable faster fast-NC policy.
Measured ONE POLICY AT A TIME (concurrent runs inflate ms 4-5x) -> honest ms/game. heur-d0 + teacher bracket it.

  scripts/d0_model_vs_rollout.py [--games N] [--seeds ...]
"""
import argparse, os, re, subprocess, time
MTG = "build/Release/mtg"
DECKS = {"antilife": ("decks/Anti-Lifegain.cod", 8, "/tmp/antilife_base.dyn"),
         "TH":       ("decks/treasure_hunt.txt", 8, "/tmp/TH_base.dyn")}
CLEAR = ["MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_DYN_MODEL",
         "MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH","MTG_D0_LANDFOLD","MTG_D0LF_K"]


def run(deck, depth, extra, games, seed, mt):
    env = {k: v for k, v in os.environ.items() if k not in CLEAR}
    env.update(extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed), "--depth", str(depth),
                          "--max-turns", str(mt), "--threads", "12"], capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"played\s*:\s*(\d+)", out).group(1)); w = int(re.search(r"won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else 0.0
    return (w*a + (p-w)*(mt+1))/p, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=80)
    ap.add_argument("--seeds", type=int, nargs="+", default=[11111, 22222, 33333])
    a = ap.parse_args()
    print(f"===== Q1: MODEL d0 SCORER vs d0 HEURISTIC ROLLOUT  (games={a.games} seeds={a.seeds}) =====")
    print(f"{'deck':9s} {'policy':22s} {'meanLP':>8s} {'ms/game':>9s}   (per-seed LP)")
    for dname, (deck, mt, model) in DECKS.items():
        pol = [("heuristic d0", 0, {}),
               ("NC-d0 heur-rollout K16", 1, {"MTG_NC_SEARCH":"1","MTG_NC_K":"16","MTG_NC_DEPTH":"0"}),
               ("land-fold DYN d0 K16", 0, {"MTG_D0_LANDFOLD":"1","MTG_D0LF_K":"16","MTG_DYN_MODEL":model}),
               ("teacher NC K16 d2", 1, {"MTG_NC_SEARCH":"1","MTG_NC_K":"16","MTG_NC_DEPTH":"2"})]
        for name, depth, extra in pol:
            lps, mss = [], []
            for s in a.seeds:
                lp, ms = run(deck, depth, extra, a.games, s, mt); lps.append(lp); mss.append(ms)
            print(f"{dname:9s} {name:22s} {sum(lps)/len(lps):8.3f} {sum(mss)/len(mss):9.1f}   "
                  + " ".join("%.3f" % x for x in lps), flush=True)
        print()


if __name__ == "__main__":
    main()
