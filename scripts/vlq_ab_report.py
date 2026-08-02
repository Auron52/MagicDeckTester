#!/usr/bin/env python3
"""Paired multi-arm report for a value-leaf regeneration batch log.

THE metric is the loss-penalised average win turn (unwon scored max_turns+1), which is exactly what
the batch line's `avg=` already is -- so negative delta = the arm is BETTER. Pairing is per seed:
every arm runs identical games, so the per-seed difference cancels game-to-game variance and the
spread of those differences is the honest error bar.

Also asserts the seed-tiling invariant (A/B rule 7): per-game identity is base_seed + game_index, so
bases spaced closer than games-per-job make jobs REPLAY games. A violating run reports enormous
significance off a handful of distinct games, and the tell is near-zero variance across seeds.

  usage: vlq_ab_report.py <batch.log> [baseline-arm]
"""
import math
import re
import sys
from collections import defaultdict

LINE = re.compile(r"([A-Za-z0-9]+)_s(\d+): played=(\d+) avg=([\d.]+) digest=(\w+)")


def main(path, baseline=None):
    arms = defaultdict(dict)
    for ln in open(path):
        m = LINE.match(ln.strip())
        if m:
            arms[m.group(1)][int(m.group(2))] = (int(m.group(3)), float(m.group(4)), m.group(5))

    if not arms:
        print(f"no job lines parsed from {path}")
        return 1
    names = sorted(arms)
    if baseline is None:
        baseline = "live" if "live" in arms else names[0]
    if baseline not in arms:
        print(f"baseline arm {baseline!r} not in {names}")
        return 1

    seeds = sorted(set.intersection(*(set(arms[a]) for a in names)))
    if not seeds:
        print(f"no seeds common to all arms {names}")
        return 1

    ids = set()
    for s in seeds:
        ids.update(range(s, s + arms[baseline][s][0]))
    n = sum(arms[baseline][s][0] for s in seeds)
    ok = len(ids) == n
    print(f"arms {names}   paired seeds {len(seeds)}   games/arm {n}   distinct ids {len(ids)}"
          f"   {'OK' if ok else '!! SEED OVERLAP -- result is NOT trustworthy'}")

    def wavg(a):
        return sum(arms[a][s][1] * arms[a][s][0] for s in seeds) / n

    base_avg = wavg(baseline)
    print(f"\n{'arm':<12} {'avg':<10} {'delta':<10} {'paired t':<10} {'better/worse/tied':<20} same-digest")
    for a in names:
        avg = wavg(a)
        if a == baseline:
            print(f"{a+' (base)':<12} {avg:<10.5f} {'-':<10} {'-':<10} {'-':<20} -")
            continue
        diffs = [arms[a][s][1] - arms[baseline][s][1] for s in seeds]
        mean = sum(diffs) / len(diffs)
        sd = math.sqrt(sum((x - mean) ** 2 for x in diffs) / (len(diffs) - 1)) if len(diffs) > 1 else 0.0
        se = sd / math.sqrt(len(diffs)) if sd else 0.0
        t = f"{mean/se:+.2f}" if se else "n/a"
        better = sum(1 for x in diffs if x < -1e-9)
        worse = sum(1 for x in diffs if x > 1e-9)
        same = sum(1 for s in seeds if arms[a][s][2] == arms[baseline][s][2])
        note = ""
        if same == len(seeds):
            note = "  <- BYTE-IDENTICAL to baseline (this arm is not engaging)"
        if sd < 1e-12 and same != len(seeds):
            note = "  <- ZERO VARIANCE, suspect replayed games"
        print(f"{a:<12} {avg:<10.5f} {avg-base_avg:<+10.5f} {t:<10} "
              f"{f'{better}/{worse}/{len(seeds)-better-worse}':<20} {same}/{len(seeds)}{note}")
    print("\n[negative delta = better; THE metric is avg turn-to-win with unwon scored max_turns+1]")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None))
