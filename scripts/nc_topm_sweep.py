#!/usr/bin/env python3
"""Policy-prior top-M sweep for the non-clairvoyant search (MTG_NC_TOPM).

Baseline (TOPM off) = pure reshuffle-averaged rollout NC search (the ceiling, ignores the model).
TOPM=M = the model ranks candidate plans and only the top-M get the expensive K-reshuffle rollout.
The question: does top-M preserve the rollout NC's LP (quality) while cutting wall-time (speed)?

Cross-seed aggregate LP (losses = max_turns+1) + wall-time per config, one config at a time so the
ms numbers are clean (concurrent configs inflate ms 4-5x). Run AFTER the d5 depth test frees the CPU.

  scripts/nc_topm_sweep.py                    # default antilife, K16 d2, seeds 4001/4002/9009, 40g
"""
import os, re, subprocess, sys, time

MTG   = "build/Release/mtg"
DECK  = "decks/Anti-Lifegain.cod"
MODEL = "logs/eval/antilife_teacherd2_dyn.json"
K, DEPTH, MT = 16, 2, 10
SEEDS, GAMES, THREADS = [4001, 4002, 9009], 40, 12

def run(topm):
    env = {k: v for k, v in os.environ.items() if not k.startswith("MTG_")}
    env.update({"MTG_NC_SEARCH": "1", "MTG_NC_K": str(K), "MTG_NC_DEPTH": str(DEPTH),
                "MTG_DYN_MODEL": MODEL})
    if topm:
        env["MTG_NC_TOPM"] = str(topm)
    lps = []
    t0 = time.time()
    for s in SEEDS:
        out = subprocess.run([MTG, DECK, "--games", str(GAMES), "--seed", str(s),
                              "--depth", "1", "--max-turns", str(MT), "--threads", str(THREADS)],
                             capture_output=True, text=True, env=env).stdout
        p = int(re.search(r"played\s*:\s*(\d+)", out).group(1))
        w = int(re.search(r"won\s*:\s*(\d+)", out).group(1))
        a = float(re.search(r"Avg win turn\s*:\s*([\d.]+)", out).group(1))
        lps.append((w * a + (p - w) * (MT + 1)) / p)
    return sum(lps) / len(lps), time.time() - t0

def main():
    print(f"# NC top-M sweep: {DECK} K{K} d{DEPTH}, {len(SEEDS)}x{GAMES}g, model={os.path.basename(MODEL)}",
          flush=True)
    for topm in [0, 8, 4, 2]:
        lp, dt = run(topm)
        tag = "off (pure rollout)" if topm == 0 else f"top-{topm}"
        print(f"TOPM={topm:<2} [{tag:<18}]  LP={lp:.3f}  ({len(SEEDS)*GAMES} games, {dt:.0f}s)", flush=True)

if __name__ == "__main__":
    main()
