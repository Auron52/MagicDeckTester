#!/usr/bin/env python3
"""PAIRED report for the <arm>.<deck>.<block> manifests (gen_order_v2_manifest.py and friends).

compare.py and cost.py both assume a two-part <arm>.<block> job name and silently produce NOTHING
when handed a three-part one -- cost.py's block key is `name.split(".")[1]`, which on
"ord.hinata.hold" is the DECK, so it looks for a baseline called "base.hinata" and finds none. That
silent empty output is the failure mode this script exists to remove: it groups on (deck, block)
explicitly and reports quality and deterministic work side by side, because the adoption bar needs
both and condemnation is a WORK lever first.

Conventions honoured (each one has cost this repo a wrong verdict before):
  * primary metric is AVG WIN TURN with an unwon game scored max_turns+1 = 9; .wins encodes -1;
  * pairing is on the GLOBAL GAME INDEX intersection, never file position -- so a 1,200-game arm
    pairs correctly against a salvaged 3,000-game baseline;
  * se is the se of the per-game DIFFERENCE (paired), not of two independent means;
  * work is read from .units (deterministic), never from batch ms (WALL time, measured at 16.5 s
    and 48.9 s for one identical workload on this box);
  * a zero delta with an IDENTICAL digest set is reported as "inert", a different finding from
    "changed play, same turns".
"""
import pathlib
import sys
from math import sqrt

MAX_TURNS = 8


def load_wins(path):
    out = {}
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            gi, wt = int(p[0]), int(p[1])
            out[gi] = (MAX_TURNS + 1 if wt < 0 else wt, p[2] if len(p) > 2 else "")
    return out


def load_units(path):
    out = {}
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = int(p[1])
    return out


def stats(diffs):
    n = len(diffs)
    m = sum(diffs) / n
    se = (sqrt(sum((d - m) ** 2 for d in diffs) / (n - 1) / n)) if n > 1 else float("nan")
    return m, se, (m / se if se else float("nan"))


def main():
    root = pathlib.Path(sys.argv[1])
    baseline = sys.argv[2] if len(sys.argv) > 2 else "base"
    wins = {p.stem: load_wins(p) for p in sorted(root.glob("*.wins"))}
    units = {p.stem: load_units(p) for p in sorted(root.glob("*.units"))}
    if not wins:
        sys.exit(f"no .wins under {root}")

    cells = sorted({tuple(k.split(".")[1:3]) for k in wins if k.count(".") == 2})
    for deck, block in cells:
        bkey = f"{baseline}.{deck}.{block}"
        if bkey not in wins:
            print(f"\n=== {deck} / {block}: NO BASELINE ({bkey}) -- skipped")
            continue
        bw = wins[bkey]
        # The baseline avg is printed PER ARM below, over that arm's index intersection -- NOT once
        # over the whole baseline. A 1,200-game arm paired against a salvaged 3,000-game baseline
        # has a different baseline mean on its 1,200 games, and printing the 3,000-game mean next
        # to the arm's 1,200-game mean invites reading the delta's SIGN off the wrong subtraction.
        print(f"\n=== {deck} / {block}   baseline {bkey} ({len(bw)} games) ===")
        print(f"{'arm':<16} {'n':>5} {'base':>8} {'avg':>8} {'delta':>9} {'se':>7} {'t':>7} "
              f"{'fast':>5} {'slow':>5} {'units':>9} {'ratio':>7}")
        for key in sorted(wins):
            parts = key.split(".")
            if len(parts) != 3 or parts[1] != deck or parts[2] != block or key == bkey:
                continue
            aw = wins[key]
            ks = sorted(set(aw) & set(bw))
            # delta > 0 = the ARM wins SOONER than baseline = BETTER.
            d = [bw[k][0] - aw[k][0] for k in ks]
            m, se, t = stats(d)
            avg = sum(aw[k][0] for k in ks) / len(ks)
            bavg = sum(bw[k][0] for k in ks) / len(ks)
            fast = sum(1 for k in ks if aw[k][0] < bw[k][0])
            slow = sum(1 for k in ks if aw[k][0] > bw[k][0])
            same_digest = all(aw[k][1] == bw[k][1] for k in ks)
            ustr, rstr = "-", "-"
            if key in units and bkey in units:
                au, bu = units[key], units[bkey]
                uk = [k for k in ks if k in au and k in bu and bu[k] > 0]
                if uk:
                    tot_a, tot_b = sum(au[k] for k in uk), sum(bu[k] for k in uk)
                    ustr = f"{100.0*(tot_a-tot_b)/tot_b:+8.2f}%"
                    rstr = f"{sum(au[k]/bu[k] for k in uk)/len(uk):7.4f}"
            note = "  INERT (identical digests)" if same_digest else ""
            print(f"{parts[0]:<16} {len(ks):>5} {bavg:>8.4f} {avg:>8.4f} {m:>+9.4f} {se:>7.4f} {t:>+7.2f} "
                  f"{fast:>5} {slow:>5} {ustr:>9} {rstr:>7}{note}")
        print("  delta > 0 = arm wins SOONER (BETTER). units < 0 = LESS deterministic search work.")


if __name__ == "__main__":
    main()
