#!/usr/bin/env python3
"""Paired QUALITY + deterministic COST report for gen_greedy_adopt_manifest.py.

Job names are <arm>.<deck>.<block>, which neither compare.py nor cost.py can parse (both assume
<arm>.<block>). This reports both metrics side by side per arm per deck, because the USER's bar is
conjunctive: quality neutral-or-better AND units neutral-or-better AND no unrecoverable lines.

Conventions honoured (each of these has bitten this repo):
  * primary metric is AVG WIN TURN with an unwon game scored max_turns+1, never "a loss";
  * .wins encodes an unwon game as -1;
  * pairing is keyed on the GLOBAL GAME INDEX, not file position, and only the intersection is used;
  * cost is compared in GameWorkMeter UNITS, not batch ms -- ms is WALL time and the same workload
    has measured 16.5 s and 48.9 s on this box depending on load;
  * the per-game unit RATIO is averaged, so one 30x game cannot dominate the way a total would;
  * plays-differ is reported, because delta 0 with 0 plays-differ ("never fired") and delta 0 with
    N plays-differ ("fired, changed nothing") are different findings.
"""
import pathlib
import sys
from math import sqrt

MAX_TURNS = 8


def load_wins(p):
    out = {}
    for line in p.read_text().splitlines():
        q = line.split()
        if len(q) >= 2:
            v = int(q[1])
            out[int(q[0])] = (MAX_TURNS + 1 if v < 0 else v, q[2] if len(q) > 2 else "")
    return out


def load_units(p):
    out = {}
    for line in p.read_text().splitlines():
        q = line.split()
        if len(q) >= 2:
            out[int(q[0])] = int(q[1])
    return out


def main():
    root = pathlib.Path(sys.argv[1])
    only = sys.argv[2] if len(sys.argv) > 2 else None
    wins = {p.stem: load_wins(p) for p in root.glob("*.wins")}
    units = {p.stem: load_units(p) for p in root.glob("*.units")}
    if not wins:
        sys.exit(f"no .wins under {root}")

    decks, blocks, arms = set(), set(), set()
    for k in wins:
        parts = k.split(".")
        if len(parts) != 3:
            continue
        arms.add(parts[0]); decks.add(parts[1]); blocks.add(parts[2])

    for block in sorted(blocks):
        print(f"\n{'='*112}\n=== {block}   (delta<0 = FASTER win; ratio<1 = CHEAPER search)\n{'='*112}")
        print(f"{'deck':<16}{'arm':<15}{'n':>6}{'avg':>8}{'delta':>9}{'se':>7}{'t':>6}"
              f"{'fast':>6}{'slow':>6}{'differ':>8}{'units':>9}{'u-se':>7}{'cheap':>7}{'dear':>6}")
        for deck in sorted(decks):
            if only and deck != only:
                continue
            bk = f"base.{deck}.{block}"
            if bk not in wins:
                continue
            b, bu = wins[bk], units.get(bk, {})
            for arm in sorted(arms):
                k = f"{arm}.{deck}.{block}"
                if arm == "base" or k not in wins:
                    continue
                a = wins[k]
                ks = sorted(set(a) & set(b))
                if not ks:
                    continue
                d = [a[x][0] - b[x][0] for x in ks]
                n = len(d); m = sum(d) / n
                se = sqrt(sum((x - m) ** 2 for x in d) / (n - 1) / n) if n > 1 else float("nan")
                t = m / se if se and se == se and se > 0 else float("nan")
                dif = sum(1 for x in ks if b[x][1] and a[x][1] != b[x][1])
                au = units.get(k, {})
                uk = [x for x in ks if x in au and x in bu and bu[x] > 0]
                if uk:
                    r = [au[x] / bu[x] for x in uk]
                    um = sum(r) / len(r)
                    use = (sqrt(sum((y - um) ** 2 for y in r) / (len(r) - 1) / len(r))
                           if len(r) > 1 else float("nan"))
                    cheap = sum(1 for x in uk if au[x] < bu[x])
                    dear = sum(1 for x in uk if au[x] > bu[x])
                    ustr = f"{um:>9.4f}{use:>7.4f}{cheap:>7}{dear:>6}"
                else:
                    ustr = f"{'-':>9}{'-':>7}{'-':>7}{'-':>6}"
                print(f"{deck:<16}{arm:<15}{n:>6}{sum(a[x][0] for x in ks)/n:>8.4f}"
                      f"{m:>+9.4f}{se:>7.4f}{t:>6.2f}"
                      f"{sum(1 for x in d if x < 0):>6}{sum(1 for x in d if x > 0):>6}{dif:>8}{ustr}")

    # Suite roll-up per arm: the adoption bar is the OVERALL average, not a per-deck tally.
    print(f"\n{'='*112}\n=== SUITE ROLL-UP (all decks pooled per arm)\n{'='*112}")
    print(f"{'arm':<15}{'block':<8}{'games':>8}{'delta':>10}{'se':>8}{'t':>7}"
          f"{'faster':>8}{'slower':>8}{'mean units ratio':>19}")
    for arm in sorted(arms - {"base"}):
        for block in sorted(blocks):
            d, ratios = [], []
            for deck in sorted(decks):
                bk, k = f"base.{deck}.{block}", f"{arm}.{deck}.{block}"
                if bk not in wins or k not in wins:
                    continue
                b, a = wins[bk], wins[k]
                bu, au = units.get(bk, {}), units.get(k, {})
                for x in sorted(set(a) & set(b)):
                    d.append(a[x][0] - b[x][0])
                    if x in au and x in bu and bu[x] > 0:
                        ratios.append(au[x] / bu[x])
            if not d:
                continue
            n = len(d); m = sum(d) / n
            se = sqrt(sum((x - m) ** 2 for x in d) / (n - 1) / n) if n > 1 else float("nan")
            t = m / se if se and se == se and se > 0 else float("nan")
            ur = f"{sum(ratios)/len(ratios):.4f}" if ratios else "-"
            print(f"{arm:<15}{block:<8}{n:>8}{m:>+10.4f}{se:>8.4f}{t:>7.2f}"
                  f"{sum(1 for x in d if x < 0):>8}{sum(1 for x in d if x > 0):>8}{ur:>19}")
    print("\n  NOTE: a suite roll-up pools decks of different lengths; read it WITH the per-deck rows,")
    print("  not instead of them. Units < 1.0000 = cheaper than baseline.")


if __name__ == "__main__":
    main()
