#!/usr/bin/env python3
"""NC ladder: measure the fast-NC deliverable candidates head to head.

For each policy (heuristic-d0, NC-teacher d0/d1/d2, optional value-model land-fold), run a fixed
held-out seed set at a fixed games count and report:
  LP  = loss-penalised avg win turn (losses = max_turns+1), aggregated over seeds
  ms  = wall-clock throughput per game (subprocess wall / games), fixed thread count

The point: is NC-teacher at LOW depth already the fast NC policy close to the teacher, and where (if
anywhere) does a learned value leaf win the speed/quality frontier?

  scripts/nc_ladder.py --deck decks/treasure_hunt.txt --seeds 4001 4002 9009 9010 --games 100 \
      --k 16 --value logs/eval/TH_rsvalue_all_gbdt.value.json --dyn /tmp/TH_rsdyn_T0H128.json
"""
import argparse, os, re, subprocess, time

def base_env():
    return {kk: vv for kk, vv in os.environ.items() if not kk.startswith("MTG_")}

def run_policy(deck, seeds, games, mt, threads, extra_env, depth=0):
    # NOTE the MTG_NC_SEARCH block lives inside `if (m_lookahead_depth > 0)` in AIEngine, so NC
    # policies MUST run at --depth 1 (the dispatch gate); MTG_NC_DEPTH sets the actual NC lookahead.
    # The heuristic / land-fold value policies run at --depth 0.
    env = base_env(); env.update(extra_env)
    lps = []; tw = 0.0; tg = 0
    for s in seeds:
        t0 = time.time()
        out = subprocess.run(["build/Release/mtg", deck, "--games", str(games), "--seed", str(s),
                              "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)],
                             capture_output=True, text=True, env=env).stdout
        dt = time.time() - t0
        p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
        w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
        m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else 0.0
        lps.append((w*a + (p-w)*(mt+1)) / p)
        tw += dt; tg += p
    return sum(lps)/len(lps), 1000.0*tw/tg

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True)
    ap.add_argument("--seeds", type=int, nargs="+", default=[4001, 4002, 9009, 9010])
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--k", type=int, default=16)
    ap.add_argument("--depths", type=int, nargs="+", default=[0, 1, 2])
    ap.add_argument("--value", default=None, help="GBDT value model for land-fold")
    ap.add_argument("--dyn", default=None, help="DynNet model for land-fold")
    ap.add_argument("--d0lf-k", type=int, default=16)
    args = ap.parse_args()

    def show(label, extra, depth=0):
        lp, ms = run_policy(args.deck, args.seeds, args.games, args.max_turns, args.threads, extra, depth)
        print(f"{label:<34} LP={lp:7.3f}   ms/game={ms:7.1f}", flush=True)
        return lp, ms

    print(f"# {args.deck}  seeds={args.seeds}  games={args.games}  K={args.k}  threads={args.threads}")
    show("heuristic-d0", {})
    for d in args.depths:
        # --depth 1 is only the dispatch gate; MTG_NC_DEPTH is the real NC lookahead.
        show(f"NC-K{args.k}-d{d}", {"MTG_NC_SEARCH": "1", "MTG_NC_K": str(args.k), "MTG_NC_DEPTH": str(d)}, depth=1)
    if args.value:
        show(f"landfold-value-K{args.d0lf_k}",
             {"MTG_D0_LANDFOLD": "1", "MTG_VALUE_PROFILE": args.value, "MTG_D0LF_K": str(args.d0lf_k)})
    if args.dyn:
        show(f"landfold-dyn-K{args.d0lf_k}",
             {"MTG_D0_LANDFOLD": "1", "MTG_DYN_MODEL": args.dyn, "MTG_D0LF_K": str(args.d0lf_k)})

if __name__ == "__main__":
    main()
