#!/usr/bin/env python3
"""Pair two MTG_DUMP_WINS arms by game index and split the divergences.

The dump line is `[win] job=<arm> gi=<n> wt=<turn>`, one per game, emitted by every
batch worker with no ordering guarantee -- so pairing is on `gi`, never on file order.
`wt=-1` is an unwon game; it scores as `max_turns + 1` (the repo's primary objective is
avg win turn with losses penalised, not win%).

Prints the paired average for each arm and, for every game whose win turn MOVED, a repro
command. `--seed base+gi --game-index gi --games 1` is the only form that replays the same
game: `--seed base+gi` alone plays a different one.

    python3 scripts/win_divergence.py <wins.log> --base-arm base --alt-arm clock --seed 70000
"""
import argparse
import collections
import re
import sys

LINE = re.compile(r"^\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--base-arm", required=True)
    ap.add_argument("--alt-arm", required=True)
    ap.add_argument("--seed", type=int, required=True, help="the manifest's seed for BOTH arms")
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--deck", default="decks/Fungus/Fungus.cod")
    ap.add_argument("--profile", default="decks/Fungus/Fungus.profile.json")
    args = ap.parse_args()

    arms = collections.defaultdict(dict)
    with open(args.log, errors="replace") as fh:
        for line in fh:
            m = LINE.match(line)
            if m:
                arms[m.group(1)][int(m.group(2))] = int(m.group(3))

    a, b = arms.get(args.base_arm, {}), arms.get(args.alt_arm, {})
    if not a or not b:
        sys.exit(f"missing arm: have {sorted(arms)}")

    # PAIRED only. An arm that is short (a straggler still running, a killed game) must not
    # drag the comparison: unequal game sets are how a depth-matrix comparison once came out
    # with the wrong SIGN.
    common = sorted(set(a) & set(b))
    score = lambda wt: (args.max_turns + 1) if wt < 0 else wt

    sa = sum(score(a[g]) for g in common)
    sb = sum(score(b[g]) for g in common)
    print(f"paired games      : {len(common)}   (base held {len(a)}, alt held {len(b)})")
    print(f"{args.base_arm:<18}: avg {sa / len(common):.4f}   unwon {sum(1 for g in common if a[g] < 0)}")
    print(f"{args.alt_arm:<18}: avg {sb / len(common):.4f}   unwon {sum(1 for g in common if b[g] < 0)}")
    print(f"delta (alt-base)  : {(sb - sa) / len(common):+.4f}   (negative = alt is FASTER)")

    worse = [g for g in common if score(b[g]) > score(a[g])]
    better = [g for g in common if score(b[g]) < score(a[g])]
    print(f"\nDIVERGENT: {len(worse) + len(better)}   WORSE {len(worse)}   BETTER {len(better)}")

    for tag, games in (("WORSE", worse), ("BETTER", better)):
        for g in games:
            wa = "unwon" if a[g] < 0 else f"T{a[g]}"
            wb = "unwon" if b[g] < 0 else f"T{b[g]}"
            print(f"{tag} gi={g:<6} {args.base_arm}={wa:<6} {args.alt_arm}={wb:<6} "
                  f"repro: ./build/Release/mtg {args.deck} --profile {args.profile} "
                  f"--seed {args.seed + g} --game-index {g} --games 1")


if __name__ == "__main__":
    main()
