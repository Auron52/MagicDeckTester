#!/usr/bin/env python3
"""A/B the learned mid-game evaluator against the baseline heuristic (or two models).

Runs build/Release/mtg goldfishing at a fixed depth over a seed range with the eval model
OFF (baseline) and ON (a given sidecar), and reports the PRIMARY metric: average win turn
with losses counted as max_turns+1 (lower = better), plus win%. See docs/design/learned-d0-policy.md.

    scripts/eval_ab.py --deck decks/treasure_hunt.txt --depth 0 --games 200 --seed 2002 \
        --model /tmp/th_v1.eval.json

Pass --model none to measure only the baseline. Pass --model-b to A/B two models head-to-head
(A=--model, B=--model-b) instead of vs baseline. Runs are byte-deterministic, so no repeats.
"""
import argparse, os, re, subprocess, sys

MTG = "build/Release/mtg"


def run(deck, depth, games, seed, max_turns, threads, model):
    """Return (played, won, avg_won, loss_penalized_avg). model=None => baseline (env unset)."""
    env = dict(os.environ)
    env.pop("MTG_EVAL_MODEL", None)
    env.pop("MTG_EVAL_PROFILE", None)
    env.pop("MTG_DUMP_EVAL_ROWS", None)   # never dump during A/B
    if model and model.lower() != "none":
        env["MTG_EVAL_MODEL"] = "1"
        env["MTG_EVAL_PROFILE"] = model
    cmd = [MTG, deck, "--games", str(games), "--seed", str(seed),
           "--depth", str(depth), "--max-turns", str(max_turns), "--threads", str(threads)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    played = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    won = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    avg_won = float(m.group(1)) if m else float("nan")
    lost = played - won
    lp = (won * avg_won + lost * (max_turns + 1)) / played if won else float(max_turns + 1)
    return played, won, avg_won, lp


def fmt(tag, played, won, avg_won, lp):
    pct = 100.0 * won / played if played else 0.0
    aw = ("%.3f" % avg_won) if won else "--"
    return "%-10s played=%d won=%d (%.1f%%)  avg_win_turn(won)=%s  LOSS-PENALIZED avg=%.4f" % (
        tag, played, won, pct, aw, lp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", default="decks/treasure_hunt.txt")
    ap.add_argument("--depth", type=int, default=0)
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--model", default=None, help="sidecar for arm A (or 'none' for baseline only)")
    ap.add_argument("--model-b", default=None, help="if set, A/B model vs model-b (no baseline)")
    args = ap.parse_args()

    print("# A/B deck=%s depth=%d games=%d seed=%d max_turns=%d" % (
        args.deck, args.depth, args.games, args.seed, args.max_turns), file=sys.stderr)

    if args.model_b:
        a = run(args.deck, args.depth, args.games, args.seed, args.max_turns, args.threads, args.model)
        b = run(args.deck, args.depth, args.games, args.seed, args.max_turns, args.threads, args.model_b)
        print(fmt("A:model", *a))
        print(fmt("B:model-b", *b))
        print("# delta LP(B-A) = %+.4f (negative => B better)" % (b[3] - a[3]))
        return

    base = run(args.deck, args.depth, args.games, args.seed, args.max_turns, args.threads, None)
    print(fmt("baseline", *base))
    if args.model and args.model.lower() != "none":
        mod = run(args.deck, args.depth, args.games, args.seed, args.max_turns, args.threads, args.model)
        print(fmt("learned", *mod))
        print("# delta LP(learned-baseline) = %+.4f (negative => learned better)" % (mod[3] - base[3]))


if __name__ == "__main__":
    main()
