#!/usr/bin/env python3
"""What a PRUNE actually costs: paired work units over the IDENTICAL-PLAY subset.

Two instrument errors stand between a prune and its true number, and this tool removes the second.

ERROR 1 -- MEASURING UNDER A BUDGET. SearchBudget is denominated in the same GameWorkMeter units
reported here, and iterative deepening reinvests anything freed, so a budgeted search never RETURNS
a saving. Run the arms at budget_ms=0 (gen_condemn_unbudgeted_manifest.py). This tool cannot detect
that mistake for you -- check the batch's `[play] ... budget=0ms` line yourself.

ERROR 2 -- COMPARING GAMES WHOSE PLAY DIVERGED, which is what this tool fixes. Once a prune changes
which plan is chosen, the two arms play DIFFERENT games, and the per-game unit comparison stops
being "the same tree with and without branches". It becomes two unrelated trajectories, and one
pathological game swamps everything: on Hinata's hold block, unbudgeted, ONE game (gi=717) was 27.1%
of the block's entire unit total and the top three were 52%. That single game turned a real -0.53%
saving into a reported +7.98% COST.

So: split on the per-game DIGEST (the .wins third column). Where the digest matches, the arms played
the same game and every unit of difference is the prune doing its job. Where it does not, the
comparison is reported separately and should be read as a quality/variance question, never as cost.

The subset is not small -- ~91% of Hinata's games had identical play -- and the two independent
seed blocks agreed to two decimal places (-0.58% / -0.53% total, 0.9898 / 0.9897 mean ratio), which
is the check that the number is real rather than tail noise.

Usage:  prune_cost.py <dir> <baseline-arm> [deck]
"""
import pathlib
import sys
from math import sqrt


def load_units(p):
    out = {}
    for line in p.read_text().splitlines():
        f = line.split()
        if len(f) >= 2:
            out[int(f[0])] = int(f[1])
    return out


def load_wins(p):
    out = {}
    for line in p.read_text().splitlines():
        f = line.split()
        if len(f) >= 3:
            out[int(f[0])] = (int(f[1]), f[2])
    return out


def summarise(ua, ub, keys):
    tb = sum(ub[k] for k in keys)
    ta = sum(ua[k] for k in keys)
    r = [ua[k] / ub[k] for k in keys if ub[k] > 0]
    m = sum(r) / len(r)
    se = sqrt(sum((x - m) ** 2 for x in r) / (len(r) - 1) / len(r)) if len(r) > 1 else float("nan")
    return (100.0 * (ta - tb) / tb if tb else 0.0, m, se,
            sum(1 for k in keys if ua[k] < ub[k]),
            sum(1 for k in keys if ua[k] > ub[k]),
            sum(1 for k in keys if ua[k] == ub[k]))


def main():
    root = pathlib.Path(sys.argv[1])
    baseline = sys.argv[2]
    units = {p.stem: load_units(p) for p in sorted(root.glob("*.units"))}
    wins = {p.stem: load_wins(p) for p in sorted(root.glob("*.wins"))}
    cells = sorted({tuple(k.split(".")[1:3]) for k in units if k.count(".") == 2})

    for deck, block in cells:
        bkey = f"{baseline}.{deck}.{block}"
        if bkey not in units or bkey not in wins:
            continue
        ub, wb = units[bkey], wins[bkey]
        print(f"\n=== {deck} / {block}   baseline {bkey} ===")
        print(f"{'arm':<16} {'subset':<14} {'n':>5} {'total':>9} {'mean ratio':>11} {'se':>8} "
              f"{'cheap':>6} {'dear':>5} {'same':>5}")
        for key in sorted(units):
            parts = key.split(".")
            if len(parts) != 3 or parts[1] != deck or parts[2] != block or key == bkey:
                continue
            ua, wa = units[key], wins.get(key, {})
            ks = [k for k in sorted(set(ua) & set(ub)) if k in wa and k in wb]
            same = [k for k in ks if wa[k][1] == wb[k][1]]
            diff = [k for k in ks if wa[k][1] != wb[k][1]]
            for label, sub in (("IDENTICAL play", same), ("diverged", diff)):
                if not sub:
                    continue
                tot, m, se, ch, de, eq = summarise(ua, ub, sub)
                print(f"{parts[0]:<16} {label:<14} {len(sub):>5} {tot:>+8.2f}% {m:>11.4f} "
                      f"{se:>8.4f} {ch:>6} {de:>5} {eq:>5}")
        # Totals are unusable when one game is a quarter of the block; say so with the number.
        top = sorted(ub.items(), key=lambda kv: -kv[1])[:3]
        tot_b = sum(ub.values())
        share = ", ".join(f"gi={g} {100.0*u/tot_b:.1f}%" for g, u in top)
        print(f"  baseline tail (top 3 games' share of block units): {share}")
        print("  Read the IDENTICAL-play mean ratio. 'diverged' is a quality question, not a cost one.")


if __name__ == "__main__":
    main()
