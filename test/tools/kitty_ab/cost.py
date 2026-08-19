#!/usr/bin/env python3
"""PAIRED per-game SEARCH COST comparison, in deterministic work units.

Wall-clock cannot answer this question on this box. Batch per-game ms is WALL, not CPU, and this
repo has measured the same workload at 16.5 s and 48.9 s depending on what else was running -- so a
5% "cost win" read off job ms is noise. Work units are the search's own deterministic step counter
(GameWorkMeter): identical inputs give identical units on any machine, at any load, so a paired
per-game ratio is exact rather than approximate.

Reads <name>.units (written under MTG_DUMP_UNITS alongside --game-log-dir).
"""
import pathlib
import sys
from math import sqrt


def load(path):
    out = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2:
            out[int(parts[0])] = int(parts[1])
    return out


def main():
    root = pathlib.Path(sys.argv[1])
    baseline = sys.argv[2] if len(sys.argv) > 2 else "base"
    units = {p.stem: load(p) for p in sorted(root.glob("*.units"))}
    if not units:
        sys.exit(f"no .units under {root} (was MTG_DUMP_UNITS=1 set?)")
    blocks = sorted({k.split(".")[1] for k in units if "." in k})

    for block in blocks:
        base_key = f"{baseline}.{block}"
        if base_key not in units:
            continue
        b = units[base_key]
        print(f"\n=== {block}   baseline {base_key}: total {sum(b.values()):,} units "
              f"over {len(b)} games ===")
        print(f"{'arm':<8} {'n':>4} {'total units':>14} {'vs base':>9} {'mean ratio':>11} "
              f"{'se':>7} {'cheaper':>8} {'dearer':>7} {'same':>6}")
        for key in sorted(units):
            if not key.endswith("." + block) or key == base_key:
                continue
            a = units[key]
            keys = sorted(set(a) & set(b))
            tot_b = sum(b[k] for k in keys)
            tot_a = sum(a[k] for k in keys)
            # Per-game ratio, so one 30x game cannot dominate the way a total does.
            ratios = [a[k] / b[k] for k in keys if b[k] > 0]
            m = sum(ratios) / len(ratios)
            se = (sqrt(sum((r - m) ** 2 for r in ratios) / (len(ratios) - 1) / len(ratios))
                  if len(ratios) > 1 else float("nan"))
            print(f"{key.split('.')[0]:<8} {len(keys):>4} {tot_a:>14,} "
                  f"{100.0*(tot_a-tot_b)/tot_b:>+8.2f}% {m:>11.4f} {se:>7.4f} "
                  f"{sum(1 for k in keys if a[k] < b[k]):>8} "
                  f"{sum(1 for k in keys if a[k] > b[k]):>7} "
                  f"{sum(1 for k in keys if a[k] == b[k]):>6}")
        print("  'vs base' < 0 = LESS search work. mean ratio < 1 = cheaper on the typical game.")


if __name__ == "__main__":
    main()
