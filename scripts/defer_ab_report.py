#!/usr/bin/env python3
"""Compare the defer-the-drop A/B arms game-by-game.

Metric is the repo's: avg turn-to-win with an unwon game scored as max_turns+1
(a `.wins` entry of -1 means loss -> 9).

The headline number is the per-key delta, but the load-bearing check is the
STRANDING check the USER asked for: the defer rule is only safe if it never
strands main-1 mana, so we count games that got WORSE under defer_on and report
their distribution -- a one-sided worse-bucket is a defect, a symmetric one is
churn (this arc's symmetry rule).
"""
import sys, os, collections

LOSS = 9

def load(path):
    out = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 2:
                continue
            gi, wt = int(p[0]), int(p[1])
            out[gi] = LOSS if wt < 0 else wt
    return out

def main():
    on_dir  = sys.argv[1] if len(sys.argv) > 1 else "logs/fvw_arc/defer_on/wins"
    off_dir = sys.argv[2] if len(sys.argv) > 2 else "logs/fvw_arc/defer_off/wins"

    keys = sorted(set(os.listdir(on_dir)) & set(os.listdir(off_dir)))
    keys = [k for k in keys if k.endswith(".wins")]
    if not keys:
        print("no comparable keys yet")
        return

    tot_on = tot_off = tot_n = 0
    better = worse = same = 0
    buckets = collections.Counter()
    per_deck = collections.defaultdict(lambda: [0.0, 0.0, 0])
    rows = []

    for k in keys:
        a, b = load(os.path.join(on_dir, k)), load(os.path.join(off_dir, k))
        gis = sorted(set(a) & set(b))
        if not gis:
            continue
        sa = sum(a[g] for g in gis)
        sb = sum(b[g] for g in gis)
        n  = len(gis)
        tot_on += sa; tot_off += sb; tot_n += n
        for g in gis:
            if   a[g] < b[g]: better += 1; buckets[(b[g], a[g])] += 1
            elif a[g] > b[g]: worse  += 1; buckets[(b[g], a[g])] += 1
            else:             same   += 1
        deck = k.split("_")[0]
        per_deck[deck][0] += sa; per_deck[deck][1] += sb; per_deck[deck][2] += n
        rows.append((k, sa / n, sb / n, (sa - sb) / n, n))

    print(f"{'key':<40} {'on':>8} {'off':>8} {'delta':>9} {'n':>6}")
    for k, ao, bo, d, n in sorted(rows, key=lambda r: r[3]):
        mark = "  <-- WORSE" if d > 1e-9 else ""
        print(f"{k[:-5]:<40} {ao:8.4f} {bo:8.4f} {d:+9.4f} {n:6d}{mark}")

    print("\n--- per deck ---")
    for deck, (sa, sb, n) in sorted(per_deck.items()):
        print(f"{deck:<16} on={sa/n:.4f}  off={sb/n:.4f}  delta={(sa-sb)/n:+.4f}  n={n}")

    print("\n--- overall ---")
    print(f"keys={len(rows)}  games={tot_n}")
    print(f"on ={tot_on/tot_n:.4f}   off={tot_off/tot_n:.4f}   NET DELTA={(tot_on-tot_off)/tot_n:+.4f}"
          "   (negative = defer WINS)")
    keys_better = sum(1 for r in rows if r[3] < -1e-9)
    keys_worse  = sum(1 for r in rows if r[3] >  1e-9)
    print(f"keys better={keys_better}  worse={keys_worse}  identical={len(rows)-keys_better-keys_worse}")

    print("\n--- per-game symmetry (the stranding check) ---")
    print(f"games better={better}  worse={worse}  unchanged={same}")
    if worse:
        print(f"better:worse ratio = {better/worse:.2f}")
    print("\n  off -> on transitions (bucket: count)   [+ = defer made it worse]")
    for (bo, ao), c in sorted(buckets.items(), key=lambda x: -x[1]):
        sign = "+" if ao > bo else "-"
        print(f"   {bo} -> {ao}  {sign}  {c}")

if __name__ == "__main__":
    main()

# ---- worse-game ledger -------------------------------------------------------
# USER doctrine 2026-08-16: adopting on a positive NET must not mean ignoring the
# games that got worse. Emit a repro contract per regressed game so the remainder
# is a work list, not a footnote.
def worse_ledger(on_dir, off_dir, out_path):
    import os
    rows = []
    for k in sorted(os.listdir(on_dir)):
        if not k.endswith(".wins") or not os.path.exists(os.path.join(off_dir, k)):
            continue
        a, b = load(os.path.join(on_dir, k)), load(os.path.join(off_dir, k))
        stem = k[:-5]
        seed = int(stem.split("_s")[-1])
        depth = "d0" if "_d0_" in stem else ("d3" if "_d3_" in stem else "d5")
        for g in sorted(set(a) & set(b)):
            if a[g] > b[g]:
                rows.append((stem, g, b[g], a[g], seed, depth))
    with open(out_path, "w") as f:
        f.write("# regressed games: key gi was->now  |  repro\n")
        for stem, g, was, now, seed, depth in rows:
            dflag = {"d0": "--depth 0", "d3": "--depth 3 --budget-ms 10",
                     "d5": "--budget-ms 20"}[depth]
            f.write(f"{stem} gi={g} {was}->{now}\n"
                    f"    ./build/Release/mtg <deck> --seed {seed + g} --game-index {g} "
                    f"--games 1 --threads 1 {dflag} --ignore-play-profile 2>&1\n")
    return len(rows)

if __name__ == "__main__" and len(sys.argv) > 3:
    n = worse_ledger(sys.argv[1], sys.argv[2], sys.argv[3])
    print(f"\nworse-game ledger: {n} games -> {sys.argv[3]}")
