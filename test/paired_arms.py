#!/usr/bin/env python3
"""Paired per-game comparison of two per-job ARMS from one pooled `mtg --batch` run.

A deck-level average hides the thing you actually want to know: a delta of +0.008 turns
over 120 games is ONE game moving ONE turn, which no mean can distinguish from noise.
Because both arms play the SAME seeds, the comparison is PAIRED -- so read it as
"how many games moved, and which way", with the mean as a summary rather than the test.

Usage:
    python3 test/paired_arms.py <wins_dir> --base unsound --arm shipped

Expects the batch to have been run with `--game-log-dir <wins_dir>` (and, for the cost
column, `MTG_DUMP_UNITS=1`), with job names of the form `<deck>__<arm>`.

Scoring follows the repo metric: an unwon game (`-1` in the .wins file) scores as 9.
"""
import argparse
import math
import os
import sys
from collections import defaultdict


def read_wins(path, loss_turn):
    """gi -> scored win turn."""
    out = {}
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) < 2:
                continue
            gi, wt = int(parts[0]), int(parts[1])
            out[gi] = loss_turn if wt < 0 else wt
    return out


def read_units(path):
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) >= 2:
                out[int(parts[0])] = int(parts[1])
    return out


def sign_test(better, worse):
    """Two-sided exact binomial p for `worse` successes in `better+worse` trials at p=0.5."""
    n = better + worse
    if n == 0:
        return 1.0
    k = min(better, worse)
    tail = sum(math.comb(n, i) for i in range(0, k + 1)) / (2.0 ** n)
    return min(1.0, 2.0 * tail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wins_dir")
    ap.add_argument("--base", required=True, help="baseline arm suffix (e.g. unsound)")
    ap.add_argument("--arm", required=True, help="arm under test (e.g. shipped)")
    ap.add_argument("--loss-turn", type=int, default=9)
    ap.add_argument("--sort", choices=["delta", "name"], default="delta")
    ap.add_argument("--list-moved", action="store_true",
                    help="also print every game whose result differs: deck gi base arm")
    args = ap.parse_args()
    moved_rows = []

    decks = set()
    for fn in os.listdir(args.wins_dir):
        if fn.endswith(".wins") and "__" in fn:
            deck, arm = fn[: -len(".wins")].split("__", 1)
            if arm in (args.base, args.arm):
                decks.add(deck)

    rows = []
    pooled = defaultdict(int)
    pooled_d = []
    for deck in sorted(decks):
        pb = os.path.join(args.wins_dir, f"{deck}__{args.base}.wins")
        pa = os.path.join(args.wins_dir, f"{deck}__{args.arm}.wins")
        if not (os.path.exists(pb) and os.path.exists(pa)):
            print(f"  (skip {deck}: missing an arm)", file=sys.stderr)
            continue
        wb, wa = read_wins(pb, args.loss_turn), read_wins(pa, args.loss_turn)
        gis = sorted(set(wb) & set(wa))
        d = [wa[g] - wb[g] for g in gis]
        better = sum(1 for x in d if x < 0)
        worse = sum(1 for x in d if x > 0)
        n = len(d)
        mean = sum(d) / n if n else 0.0
        var = sum((x - mean) ** 2 for x in d) / (n - 1) if n > 1 else 0.0
        se = math.sqrt(var / n) if n else 0.0
        ub = read_units(os.path.join(args.wins_dir, f"{deck}__{args.base}.units"))
        ua = read_units(os.path.join(args.wins_dir, f"{deck}__{args.arm}.units"))
        cost = (sum(ua.values()) / sum(ub.values())) if ub and ua and sum(ub.values()) else float("nan")
        rows.append(dict(deck=deck, n=n, mean_b=sum(wb[g] for g in gis) / n,
                         mean_a=sum(wa[g] for g in gis) / n, delta=mean, se=se,
                         better=better, worse=worse, p=sign_test(better, worse), cost=cost,
                         units_b=sum(ub.values()), units_a=sum(ua.values())))
        pooled["better"] += better
        pooled["worse"] += worse
        pooled_d.extend(d)
        if args.list_moved:
            for g in gis:
                if wa[g] != wb[g]:
                    moved_rows.append((deck, g, wb[g], wa[g]))

    if args.sort == "delta":
        rows.sort(key=lambda r: -r["delta"])

    print(f"PAIRED  {args.arm}  vs  {args.base}      (loss scored as {args.loss_turn})")
    print(f"{'deck':<16}{'n':>6}{'base':>9}{'arm':>9}{'delta':>10}{'+/-se':>9}"
          f"{'better':>8}{'worse':>7}{'sign p':>9}{'units':>9}")
    for r in rows:
        flag = ""
        if r["p"] < 0.05:
            flag = "  <-- WORSE" if r["worse"] > r["better"] else "  <-- BETTER"
        print(f"{r['deck']:<16}{r['n']:>6}{r['mean_b']:>9.4f}{r['mean_a']:>9.4f}"
              f"{r['delta']:>+10.4f}{r['se']:>9.4f}{r['better']:>8}{r['worse']:>7}"
              f"{r['p']:>9.3f}{r['cost']:>9.4f}{flag}")

    n = len(pooled_d)
    if n:
        mean = sum(pooled_d) / n
        var = sum((x - mean) ** 2 for x in pooled_d) / (n - 1) if n > 1 else 0.0
        se = math.sqrt(var / n)
        ub = sum(r["units_b"] for r in rows)
        ua = sum(r["units_a"] for r in rows)
        print("-" * 92)
        print(f"{'ALL GAMES':<16}{n:>6}{sum(r['mean_b'] for r in rows)/len(rows):>9.4f}"
              f"{sum(r['mean_a'] for r in rows)/len(rows):>9.4f}{mean:>+10.4f}{se:>9.4f}"
              f"{pooled['better']:>8}{pooled['worse']:>7}"
              f"{sign_test(pooled['better'], pooled['worse']):>9.3f}"
              f"{(ua/ub if ub else float('nan')):>9.4f}")
        print(f"  paired mean delta = {mean:+.4f} +/- {se:.4f} (1 se)"
              f"   95% CI [{mean-1.96*se:+.4f}, {mean+1.96*se:+.4f}]"
              f"   moved {pooled['better']+pooled['worse']}/{n} games"
              f" ({100.0*(pooled['better']+pooled['worse'])/n:.2f}%)")

    if args.list_moved and moved_rows:
        print(f"\nMOVED GAMES ({len(moved_rows)})   deck gi {args.base} -> {args.arm}")
        for deck, g, b, a in sorted(moved_rows, key=lambda r: (r[0], r[1])):
            print(f"  {deck:<16} gi={g:<6} {b} -> {a}   {'WORSE' if a > b else 'better'}")


if __name__ == "__main__":
    main()
