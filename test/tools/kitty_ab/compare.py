#!/usr/bin/env python3
"""PAIRED per-game comparison of the KittyEquipment lever arms.

Reads the batch's per-job <name>.wins files (written by --game-log-dir), whose lines are
"<global_game_index> <win_turn> <digest>". Arms share seeds, so game i of every arm is the SAME
shuffle: the comparison is paired and the se is the se of the per-game DIFFERENCE, which is far
tighter than comparing two independent means.

Conventions this repo has been bitten by, honoured here:
  * the primary metric is AVG WIN TURN, and an unwon game counts as max_turns+1 (never "a loss");
  * .wins encodes an unwon game as -1;
  * pairing is keyed on the GLOBAL GAME INDEX, not file position, and only the intersection is
    compared -- an unequal game set has flipped the sign of a comparison in this repo before;
  * the DIGEST column is reported too: a zero delta with an identical digest set means the lever
    did not change play at all, which is a different finding from "changed play, same turns".
"""
import pathlib
import sys
from math import sqrt

MAX_TURNS = 8   # goldfish horizon; an unwon game scores MAX_TURNS+1


def load(path):
    """-> {global_game_index: (win_turn, digest)}"""
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
    d = [b[k][0] - a[k][0] for k in keys]
    n = len(d)
    mean = sum(d) / n
    se = sqrt(sum((x - mean) ** 2 for x in d) / (n - 1) / n) if n > 1 else float("nan")
    dig_diff = sum(1 for k in keys if a[k][1] and a[k][1] != b[k][1])
    return {
        "n": n, "mean": mean, "se": se,
        "better": sum(1 for x in d if x < 0),
        "worse": sum(1 for x in d if x > 0),
        "dig_diff": dig_diff,
        "avg": sum(b[k][0] for k in keys) / n,
        "dropped": len(set(a) ^ set(b)),
    }


def main():
    root = pathlib.Path(sys.argv[1])
    baseline = sys.argv[2] if len(sys.argv) > 2 else "base"
    wins = {p.stem: load(p) for p in sorted(root.glob("*.wins"))}
    if not wins:
        sys.exit(f"no .wins under {root}")
    blocks = sorted({k.split(".")[1] for k in wins if "." in k})

    for block in blocks:
        base_key = f"{baseline}.{block}"
        if base_key not in wins:
            continue
        b = wins[base_key]
        bavg = sum(v[0] for v in b.values()) / len(b)
        print(f"\n=== {block}   baseline {base_key}: n={len(b)}  avg={bavg:.4f} ===")
        print(f"{'arm':<8} {'n':>4} {'avg':>7} {'delta':>9} {'se':>7} {'t':>6} "
              f"{'faster':>7} {'slower':>7} {'plays-differ':>13} {'unpaired':>9}")
        for key in sorted(wins):
            if not key.endswith("." + block) or key == base_key:
                continue
            r = paired(b, wins[key])
            t = r["mean"] / r["se"] if r["se"] and r["se"] == r["se"] and r["se"] > 0 else float("nan")
            print(f"{key.split('.')[0]:<8} {r['n']:>4} {r['avg']:>7.4f} {r['mean']:>+9.4f} "
                  f"{r['se']:>7.4f} {t:>6.2f} {r['better']:>7} {r['worse']:>7} "
                  f"{r['dig_diff']:>13} {r['dropped']:>9}")
        print("  delta < 0 = FASTER than baseline (the improvement direction).")
        print("  plays-differ = games whose decision stream changed at all; 0 there means the lever "
              "never fired.")


if __name__ == "__main__":
    main()
