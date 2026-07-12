#!/usr/bin/env python3
"""Policy-prior top-M sweep for the non-clairvoyant search (MTG_NC_TOPM).

Baseline (TOPM off) = pure reshuffle-averaged rollout NC search (the ceiling, ignores the model).
TOPM=M = the model ranks candidate plans and only the top-M get the expensive K-reshuffle rollout.
Question: does top-M preserve the rollout NC's LP (quality) while cutting wall-time (speed)?

Cross-seed aggregate LP (losses = max_turns+1) + wall-time per config, one config at a time so the
ms numbers are clean (concurrent configs inflate ms 4-5x). Run on a QUIET machine for honest speed.

  scripts/nc_topm_sweep.py --deck decks/burn.txt --value decks/burn.value.json --max-turns 8
  scripts/nc_topm_sweep.py --deck decks/Anti-Lifegain.cod --dyn logs/eval/antilife_teacherd2_dyn.json \
      --max-turns 10 --seeds 4001 4002 9009 2002 3003 7007
"""
import argparse, os, re, subprocess, time

MTG = "build/Release/mtg"

def run(deck, env_model, topm, K, depth, mt, seeds, games, threads):
    env = {k: v for k, v in os.environ.items() if not k.startswith("MTG_")}
    env.update({"MTG_NC_SEARCH": "1", "MTG_NC_K": str(K), "MTG_NC_DEPTH": str(depth)})
    env.update(env_model)
    if topm:
        env["MTG_NC_TOPM"] = str(topm)
    lps, wins, played = [], 0, 0
    t0 = time.time()
    for s in seeds:
        try:
            out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(s),
                                  "--depth", "1", "--max-turns", str(mt), "--threads", str(threads)],
                                 capture_output=True, text=True, env=env, timeout=1800).stdout
        except subprocess.TimeoutExpired:
            return None, time.time() - t0, wins, played  # batch too slow -> flag, skip this config
        m = re.search(r"played\s*:\s*(\d+)", out)
        if m is None:
            return None, time.time() - t0, wins, played
        p = int(m.group(1))
        w = int(re.search(r"won\s*:\s*(\d+)", out).group(1))
        a = float(re.search(r"Avg win turn\s*:\s*([\d.]+)", out).group(1))
        lps.append((w * a + (p - w) * (mt + 1)) / p)
        wins += w; played += p
    return sum(lps) / len(lps), time.time() - t0, wins, played

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True)
    ap.add_argument("--dyn", default=None)
    ap.add_argument("--value", default=None)
    ap.add_argument("-K", type=int, default=16)
    ap.add_argument("--depth", type=int, default=2)
    ap.add_argument("--max-turns", type=int, default=10)
    ap.add_argument("--seeds", type=int, nargs="+", default=[4001, 4002, 9009])
    ap.add_argument("--games", type=int, default=40)
    ap.add_argument("--threads", type=int, default=12)
    ap.add_argument("--topm", type=int, nargs="+", default=[0, 8, 4, 2, 1])
    args = ap.parse_args()
    env_model = {}
    if args.dyn:   env_model["MTG_DYN_MODEL"] = args.dyn
    if args.value: env_model["MTG_VALUE_PROFILE"] = args.value
    model = args.dyn or args.value or "(none)"
    n = len(args.seeds) * args.games
    print(f"# NC top-M sweep: {os.path.basename(args.deck)} K{args.K} d{args.depth} mt{args.max_turns}, "
          f"{len(args.seeds)}x{args.games}={n}g, model={os.path.basename(model)}", flush=True)
    base_lp = None
    for topm in args.topm:
        lp, dt, w, p = run(args.deck, env_model, topm, args.K, args.depth, args.max_turns,
                           args.seeds, args.games, args.threads)
        tag = "off (pure rollout)" if topm == 0 else f"top-{topm}"
        if lp is None:
            print(f"TOPM={topm:<2} [{tag:<18}]  TIMEOUT/ERR after {dt:.0f}s (skipped)", flush=True)
            continue
        if topm == 0: base_lp = lp
        dlp = "" if base_lp is None else f"  dLP={lp - base_lp:+.3f}"
        print(f"TOPM={topm:<2} [{tag:<18}]  LP={lp:.3f}  won={w}/{p}  ({dt:.0f}s){dlp}", flush=True)

if __name__ == "__main__":
    main()
