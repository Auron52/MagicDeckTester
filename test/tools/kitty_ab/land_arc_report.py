#!/usr/bin/env python3
"""Paired per-game report for the pooled land arc (gen_land_arc_manifest.py).

compare.py cannot read this batch's job names: it splits on "." and assumes <arm>.<block>, while the
pooled manifest carries two differently-shaped halves in one queue --

    A.<arm>.<block>            Mirrorwing land condemnation, baseline A.base
    B.<arm>.<deck>.<block>     suite-wide rollout land ranker, baseline B.rbase

The pairing math is compare.py's, unchanged, and so are the conventions it honours:
  * primary metric is AVG WIN TURN with an unwon game scored max_turns+1 (never "a loss");
  * .wins encodes an unwon game as -1;
  * pairing is keyed on the GLOBAL GAME INDEX, not file position, and only the intersection is
    compared -- an unequal game set has flipped the sign of a comparison in this repo before;
  * plays-differ (digest mismatches) is reported alongside the delta, because "0.0000 with 0
    plays-differ" (the lever never fired) and "0.0000 with N plays-differ" (it fired and changed
    nothing) are different findings, and this arc has already produced three vacuous nulls.
"""
import pathlib
import sys
from math import sqrt

MAX_TURNS = 8   # goldfish horizon; an unwon game scores MAX_TURNS+1


def load(path):
    out = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        gi, wt = int(parts[0]), int(parts[1])
        out[gi] = (MAX_TURNS + 1 if wt < 0 else wt, parts[2] if len(parts) > 2 else "")
    return out


def paired(a, b):
    keys = sorted(set(a) & set(b))
    if not keys:
        return None
    d = [b[k][0] - a[k][0] for k in keys]
    n = len(d)
    mean = sum(d) / n
    se = sqrt(sum((x - mean) ** 2 for x in d) / (n - 1) / n) if n > 1 else float("nan")
    return {
        "n": n, "mean": mean, "se": se,
        "better": sum(1 for x in d if x < 0),
        "worse": sum(1 for x in d if x > 0),
        "dig_diff": sum(1 for k in keys if a[k][1] and a[k][1] != b[k][1]),
        "avg": sum(b[k][0] for k in keys) / n,
        "dropped": len(set(a) ^ set(b)),
    }


HDR = (f"{'arm':<16}{'n':>7}{'avg':>9}{'delta':>10}{'se':>8}{'t':>7}"
       f"{'faster':>8}{'slower':>8}{'plays-differ':>14}{'unpaired':>10}")


def row(label, r):
    t = r["mean"] / r["se"] if r["se"] and r["se"] == r["se"] and r["se"] > 0 else float("nan")
    print(f"{label:<16}{r['n']:>7}{r['avg']:>9.4f}{r['mean']:>+10.4f}{r['se']:>8.4f}{t:>7.2f}"
          f"{r['better']:>8}{r['worse']:>8}{r['dig_diff']:>14}{r['dropped']:>10}")


def main():
    root = pathlib.Path(sys.argv[1])
    wins = {p.stem: load(p) for p in sorted(root.glob("*.wins"))}
    if not wins:
        sys.exit(f"no .wins under {root}")

    # ---- HALF A: A.<arm>.<block>, baseline A.base -------------------------------------------
    a_keys = [k for k in wins if k.startswith("A.")]
    blocks = sorted({k.split(".")[-1] for k in a_keys})
    for block in blocks:
        base = f"A.base.{block}"
        if base not in wins:
            continue
        b = wins[base]
        print(f"\n=== A  MIRRORWING land condemnation   {block}   baseline {base}: "
              f"n={len(b)} avg={sum(v[0] for v in b.values()) / len(b):.4f} ===")
        print(HDR)
        for k in sorted(a_keys):
            if not k.endswith("." + block) or k == base:
                continue
            r = paired(b, wins[k])
            if r:
                row(k[len("A."):-len("." + block)], r)

    # ---- HALF B: B.<arm>.<deck>.<block>, baseline B.rbase ------------------------------------
    b_keys = [k for k in wins if k.startswith("B.")]
    decks = sorted({k.split(".")[2] for k in b_keys})
    r_blocks = sorted({k.split(".")[-1] for k in b_keys})
    for block in r_blocks:
        print(f"\n=== B  ROLLOUT land ranker (MTG_ROLLOUT_LAND_RANKER)   {block} ===")
        print(HDR)
        tot_n = tot_sum = 0
        for deck in decks:
            base, arm = f"B.rbase.{deck}.{block}", f"B.rshared.{deck}.{block}"
            if base not in wins or arm not in wins:
                continue
            r = paired(wins[base], wins[arm])
            if not r:
                continue
            row(deck, r)
            tot_n += r["n"]
            tot_sum += r["mean"] * r["n"]
        if tot_n:
            print(f"{'-' * 97}\n{'SUITE (games)':<16}{tot_n:>7}{'':>9}{tot_sum / tot_n:>+10.4f}")

    print("\n  delta < 0 = FASTER than baseline (the improvement direction).")
    print("  plays-differ 0 with delta 0 means the lever NEVER FIRED -- not that it is neutral.")
    print("  burn is the built-in negative control for half B: its greedy drop is 100% forced,")
    print("  so a nonzero delta there means the lever does something other than it claims.")


if __name__ == "__main__":
    main()
