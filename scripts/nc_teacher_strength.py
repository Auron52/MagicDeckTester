#!/usr/bin/env python3
"""Goldfish teacher-strength sweep: does the NC teacher improve past K16 d2?

Runs the NC reshuffle search at several (K, depth) settings and reports loss-penalised LP + wall-clock
throughput on held-out seeds. Answers "are we using a high enough search level?" faster than the
forced-hand ref_bench (which times out at high K/depth on combo decks). NC runs at --depth 1 (the
dispatch gate); MTG_NC_DEPTH is the real lookahead.

  scripts/nc_teacher_strength.py --deck decks/Anti-Lifegain.cod --seeds 4001 4002 9009 --games 80
"""
import argparse, os, re, subprocess, time

def run(deck, seeds, games, mt, k, d, threads):
    env = {kk: v for kk, v in os.environ.items() if not kk.startswith("MTG_")}
    env.update({"MTG_NC_SEARCH": "1", "MTG_NC_K": str(k), "MTG_NC_DEPTH": str(d)})
    lps = []; tw = 0.0; tg = 0
    for s in seeds:
        t0 = time.time()
        out = subprocess.run(["build/Release/mtg", deck, "--games", str(games), "--seed", str(s),
                              "--depth", "1", "--max-turns", str(mt), "--threads", str(threads)],
                             capture_output=True, text=True, env=env).stdout
        dt = time.time() - t0
        p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
        w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
        m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else 0.0
        lps.append((w*a + (p-w)*(mt+1)) / p); tw += dt; tg += p
    return sum(lps)/len(lps), 1000.0*tw/tg

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True)
    ap.add_argument("--seeds", type=int, nargs="+", default=[4001, 4002, 9009])
    ap.add_argument("--games", type=int, default=80)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--configs", nargs="+", default=["16:2", "32:2", "16:3", "32:3"])
    args = ap.parse_args()
    print(f"# {args.deck} teacher strength  seeds={args.seeds} games={args.games}")
    for c in args.configs:
        k, d = (int(x) for x in c.split(":"))
        lp, ms = run(args.deck, args.seeds, args.games, args.max_turns, k, d, args.threads)
        print(f"NC-K{k}-d{d:<2}  LP={lp:7.3f}  ms/game={ms:7.1f}", flush=True)

if __name__ == "__main__":
    main()
