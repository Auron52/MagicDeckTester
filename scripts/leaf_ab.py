#!/usr/bin/env python3
"""Q2: is the learned VALUE NET a better SEARCH LEAF than the heuristic rollout?

The default (clairvoyant) search evaluates a leaf either by rolling the greedy heuristic to the end
(O(turns)) or by the O(1) value net (MTG_VALUE_MODEL=1 + <deck>.value.json, the shipped hybrid leaf with
per-model trust-depth escalation). Same search, same depth -> ONLY the leaf differs. If the value leaf holds
LP while cutting ms, it is a strictly better leaf -> adoptable. Measured ONE POLICY AT A TIME (honest ms).

  scripts/leaf_ab.py [--games N] [--depths 3 5] [--seeds ...]
"""
import argparse, os, re, subprocess, time
MTG = "build/Release/mtg"
DECKS = {"antilife": ("decks/Anti-Lifegain.cod", 8, "decks/Anti-Lifegain.value.json"),
         "TH":       ("decks/treasure_hunt.txt", 8, "decks/treasure_hunt.value.json"),
         "knights":  ("decks/Knights.cod",       8, "decks/Knights.value.json"),
         "slivers":  ("decks/slivers_vial.txt",  8, "decks/slivers_vial.value.json"),
         "burn":     ("decks/burn.txt",          8, "decks/burn.value.json")}
CLEAR = ["MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_DYN_MODEL",
         "MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH","MTG_D0_LANDFOLD","MTG_D0LF_K","MTG_VALUE_MIN_DEPTH"]


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
    ap.add_argument("--games", type=int, default=60)
    ap.add_argument("--depths", type=int, nargs="+", default=[3, 5])
    ap.add_argument("--seeds", type=int, nargs="+", default=[11111, 22222, 33333])
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    a = ap.parse_args()
    print(f"===== Q2: VALUE-NET LEAF vs HEURISTIC-ROLLOUT LEAF  (games={a.games} seeds={a.seeds}) =====")
    print(f"{'deck':9s} {'depth':>5s} {'leaf':16s} {'meanLP':>8s} {'ms/game':>9s}   (per-seed LP)")
    for dname in a.decks:
        deck, mt, prof = DECKS[dname]
        for D in a.depths:
            for name, extra in [("heur-rollout", {"MTG_VALUE_MODEL":"0"}),
                                ("value-net", {"MTG_VALUE_MODEL":"1","MTG_VALUE_PROFILE":prof})]:
                lps, mss = [], []
                for s in a.seeds:
                    lp, ms = run(deck, D, extra, a.games, s, mt); lps.append(lp); mss.append(ms)
                print(f"{dname:9s} {D:5d} {name:16s} {sum(lps)/len(lps):8.3f} {sum(mss)/len(mss):9.1f}   "
                      + " ".join("%.3f" % x for x in lps), flush=True)
            print()


if __name__ == "__main__":
    main()
