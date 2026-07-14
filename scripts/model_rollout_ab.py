#!/usr/bin/env python3
"""Model-based leaf ROLLOUT vs heuristic leaf rollout (MTG_MODEL_ROLLOUT).

A rollout SAMPLES the reshuffled library regardless of the policy driving it, so a model playout -- unlike
the O(1) value-net leaf -- is NOT draw-limited. Question: is the model a better PLAYOUT policy than the
heuristic, and at what speed? Both policies enumerate-and-score candidates per rollout turn (heuristic:
SolveWithLookahead; model: SolveD0LandFold K=1), so the cost may be closer than expected. Measured ONE
policy at a time (honest ms). NC-d0 first (rollout IS the leaf); optionally the K16 d2 teacher.

  scripts/model_rollout_ab.py [--games N] [--seeds ...] [--teacher]
"""
import argparse, os, re, subprocess, time
MTG = "build/Release/mtg"
DECKS = {"antilife": ("decks/Anti-Lifegain.cod", 8, "/tmp/antilife_base.dyn"),
         "TH":       ("decks/treasure_hunt.txt", 8, "/tmp/TH_base.dyn")}
CLEAR = ["MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_DYN_MODEL",
         "MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH","MTG_D0_LANDFOLD","MTG_D0LF_K","MTG_MODEL_ROLLOUT",
         "MTG_VALUE_MIN_DEPTH","MTG_NC_TOPM"]


def run(deck, extra, games, seed, mt):
    env = {k: v for k, v in os.environ.items() if k not in CLEAR}
    env.update(extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed), "--depth", "1",
                          "--max-turns", str(mt), "--threads", "12"], capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"played\s*:\s*(\d+)", out).group(1)); w = int(re.search(r"won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else 0.0
    return (w*a + (p-w)*(mt+1))/p, 1000.0*dt/p, w, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=80)
    ap.add_argument("--seeds", type=int, nargs="+", default=[11111, 22222, 33333])
    ap.add_argument("--teacher", action="store_true")
    a = ap.parse_args()
    print(f"===== MODEL vs HEURISTIC leaf ROLLOUT  (games={a.games} seeds={a.seeds}) =====")
    print(f"{'deck':9s} {'policy':26s} {'meanLP':>8s} {'ms/game':>9s} {'won%':>6s}   (per-seed LP)")
    for dname, (deck, mt, model) in DECKS.items():
        base_nc = {"MTG_NC_SEARCH":"1","MTG_NC_K":"16","MTG_DYN_MODEL":model}
        pol = [("NC-d0 heur-playout",  {**base_nc, "MTG_NC_DEPTH":"0"}),
               ("NC-d0 MODEL-playout", {**base_nc, "MTG_NC_DEPTH":"0", "MTG_MODEL_ROLLOUT":"1"})]
        if a.teacher:
            pol += [("teacher-d2 heur-leaf",  {**base_nc, "MTG_NC_DEPTH":"2"}),
                    ("teacher-d2 MODEL-leaf", {**base_nc, "MTG_NC_DEPTH":"2", "MTG_MODEL_ROLLOUT":"1"})]
        for name, extra in pol:
            lps, mss, ws, ps = [], [], 0, 0
            for s in a.seeds:
                lp, ms, w, p = run(deck, extra, a.games, s, mt); lps.append(lp); mss.append(ms); ws += w; ps += p
            print(f"{dname:9s} {name:26s} {sum(lps)/len(lps):8.3f} {sum(mss)/len(mss):9.1f} {100*ws/ps:6.1f}   "
                  + " ".join("%.3f" % x for x in lps), flush=True)
        print()


if __name__ == "__main__":
    main()
