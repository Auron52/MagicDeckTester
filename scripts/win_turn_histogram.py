#!/usr/bin/env python3
"""Win-turn counts from a batch .wins log.

Reads "<game_index> <win_turn> [<digest>]" lines (the per-game ground-truth log
written by `mtg --batch --game-log-dir D`) and prints the distribution.

A win_turn <= 0 means "no lethal within max_turns"; those games are their own
bucket, and they fold into the goldfish avg at max_turns+1 (see ComputeAvgTurns),
which is why the printed avg matches the batch line's own avg=.

  python3 scripts/win_turn_histogram.py logs/x/run.wins --max-turns 8
"""

import argparse
import collections
import statistics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wins")
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--width", type=int, default=48, help="bar width in columns")
    a = ap.parse_args()

    turns = []
    with open(a.wins) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                turns.append(int(parts[1]))

    won = [t for t in turns if t > 0]
    unwon = len(turns) - len(won)
    counts = collections.Counter(won)
    rows = [(f"T{t}", counts.get(t, 0)) for t in range(min(won), max(won) + 1)]
    if unwon:
        rows.append((f">T{a.max_turns}", unwon))

    # Goldfish metric: an unwon game is scored max_turns+1 (loss-penalized avg).
    scored = won + [a.max_turns + 1] * unwon
    peak = max(n for _, n in rows) or 1
    n_games = len(turns)

    print(f"{n_games:,} games\n")
    print(f"  {'turn':>5}   {'games':>7}  {'share':>6}   cum")
    cum = 0
    for label, n in rows:
        cum += n
        bar = "#" * max(1, round(n / peak * a.width)) if n else ""
        print(f"  {label:>5}   {n:>7,}  {n / n_games * 100:5.2f}%  "
              f"{cum / n_games * 100:5.1f}%  {bar}")
    print(f"\n  avg win turn (unwon scored {a.max_turns + 1}): "
          f"{sum(scored) / len(scored):.4f}"
          f"   median {statistics.median(scored):.0f}"
          f"   mean over wins only {sum(won) / len(won):.4f}")


if __name__ == "__main__":
    main()
