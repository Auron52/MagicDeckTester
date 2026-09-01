#!/usr/bin/env python3
"""PAIRED per-game arm comparison for a pooled `mtg --batch` A/B, keyed on the GLOBAL GAME INDEX.

Reads a run directory of per-job `<arm>_d<depth>_s<seed>.wins` files (the naming the Minotaur
discard sweeps use), pools every seed of a depth into one comparison, and also reports the per-seed
sign split -- because a pooled t is one number and a repo lesson says a lever must reproduce across
independent blocks, not merely clear a threshold once.

Conventions honoured (each one has cost this repo a wrong answer before):
  * the primary metric is AVG WIN TURN; an unwon game is max_turns+1, never "a loss";
  * `.wins` encodes an unwon game as -1;
  * pairing is on the game index, never file position, and only the intersection is compared;
  * the DIGEST column separates "changed play, same turns" from "never fired at all" -- a lever
    with 0 changed digests has not been measured, it has been skipped;
  * the per-seed split is over SEED-MEAN deltas, so one lucky block cannot carry a pooled mean.

Usage:  paired_arms.py <run-dir> [baseline-arm] [--max-turns N]
"""
import collections
import pathlib
import re
import sys
from math import sqrt

JOB_RE = re.compile(r"^(?P<arm>.+)_d(?P<depth>\d+)_s(?P<seed>\d+)$")


def load(path, max_turns):
    """-> {global_game_index: (win_turn, digest)}"""
    out = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        gi, wt = int(parts[0]), int(parts[1])
        out[gi] = (max_turns + 1 if wt < 0 else wt, parts[2] if len(parts) > 2 else "")
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
        "unpaired": len(set(a) ^ set(b)),
    }


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    max_turns = 8
    for a in sys.argv[1:]:
        if a.startswith("--max-turns"):
            max_turns = int(a.split("=", 1)[1]) if "=" in a else max_turns
    root = pathlib.Path(argv[0])
    baseline = argv[1] if len(argv) > 1 else "base"

    # (depth, arm) -> {seed: {gi: (wt, digest)}}
    runs = collections.defaultdict(dict)
    for p in sorted(root.glob("*.wins")):
        m = JOB_RE.match(p.stem)
        if not m:
            continue
        runs[(int(m["depth"]), m["arm"])][int(m["seed"])] = load(p, max_turns)
    if not runs:
        sys.exit(f"no <arm>_d<depth>_s<seed>.wins under {root}")

    for depth in sorted({d for d, _ in runs}):
        arms = sorted(a for d, a in runs if d == depth)
        if baseline not in arms:
            print(f"\n=== d{depth}: no baseline arm {baseline!r} (have {arms}) ===")
            continue
        base = runs[(depth, baseline)]
        pooled_base = {(s, gi): v for s, g in base.items() for gi, v in g.items()}
        # Report the baseline over the games the ARMS have actually reached, not over every game the
        # baseline happens to hold. During a PARTIAL read of a running batch those differ -- the
        # baseline runs ahead -- and a header averaging seeds no arm has played yet invites a
        # comparison against a number no arm was measured against.
        reached = set()
        for (d, a), cur in runs.items():
            if d != depth or a == baseline:
                continue
            reached |= {(s, gi) for s, g in cur.items() for gi in g}
        shared = [pooled_base[k][0] for k in pooled_base if k in reached] or \
                 [v[0] for v in pooled_base.values()]
        bavg = sum(shared) / len(shared)
        note = "" if len(shared) == len(pooled_base) else \
               f"  (of {len(pooled_base)} baseline games; PARTIAL run)"
        print(f"\n=== d{depth}   baseline {baseline}: n={len(shared)}  avg={bavg:.4f}{note} ===")
        print(f"{'arm':<8} {'n':>7} {'avg':>7} {'delta':>10} {'se':>8} {'t':>6} "
              f"{'faster':>7} {'slower':>7} {'plays-differ':>13} {'seed-blocks':>12}")
        for arm in arms:
            if arm == baseline:
                continue
            cur = runs[(depth, arm)]
            pooled_cur = {(s, gi): v for s, g in cur.items() for gi, v in g.items()}
            r = paired(pooled_base, pooled_cur)
            if r is None:
                continue
            t = r["mean"] / r["se"] if r["se"] and r["se"] > 0 else float("nan")
            # Per-seed replication: the sign of each seed's own paired mean.
            neg = pos = 0
            for s in sorted(set(base) & set(cur)):
                rs = paired(base[s], cur[s])
                if rs is None:
                    continue
                if rs["mean"] < 0:
                    neg += 1
                elif rs["mean"] > 0:
                    pos += 1
            print(f"{arm:<8} {r['n']:>7} {r['avg']:>7.4f} {r['mean']:>+10.5f} "
                  f"{r['se']:>8.5f} {t:>6.2f} {r['better']:>7} {r['worse']:>7} "
                  f"{r['dig_diff']:>13} {f'{neg}-/{pos}+':>12}")
        print("  delta < 0 = FASTER than baseline (the improvement direction).")
        print("  plays-differ = games whose digest changed; 0 means the arm never fired.")
        print("  seed-blocks  = seeds whose own paired mean is faster-/slower+.")


if __name__ == "__main__":
    main()
