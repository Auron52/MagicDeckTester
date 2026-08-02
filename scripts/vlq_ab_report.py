#!/usr/bin/env python3
"""Paired staged-vs-live report for a value-leaf regeneration A/B batch log.

THE metric is the loss-penalised average win turn (unwon scored max_turns+1), which is what the
batch line's `avg=` already is -- negative delta = staged is BETTER. Pairing is per seed: both arms
run identical games, so the per-seed difference removes game-to-game variance and the spread of
those differences is the honest error bar.

Also asserts the seed-tiling invariant (rule 7): per-game identity is base_seed + game_index, so
bases spaced closer than games-per-job make jobs replay games. A run that violates it reports
enormous significance from a handful of distinct games, and the tell is near-zero variance.
"""
import math
import re
import sys
from collections import defaultdict

LINE = re.compile(r"(\w+?)_s(\d+): played=(\d+) avg=([\d.]+) digest=(\w+)")


def main(path):
    arms = defaultdict(dict)
    for ln in open(path):
        m = LINE.match(ln.strip())
        if m:
            arm, seed, played, avg, dig = m.group(1), int(m.group(2)), int(m.group(3)), float(m.group(4)), m.group(5)
            arms[arm][seed] = (played, avg, dig)

    if set(arms) != {"live", "staged"}:
        print(f"unexpected arms in {path}: {sorted(arms)}")
        return 1

    seeds = sorted(set(arms["live"]) & set(arms["staged"]))
    if not seeds:
        print("no paired seeds")
        return 1

    ids = set()
    for s in seeds:
        ids.update(range(s, s + arms["live"][s][0]))
    n = sum(arms["live"][s][0] for s in seeds)
    print(f"paired seeds {len(seeds)}   games/arm {n}   distinct ids {len(ids)}"
          f"   {'OK' if len(ids) == n else '!! SEED OVERLAP -- result is not trustworthy'}")

    tl = sum(arms["live"][s][1] * arms["live"][s][0] for s in seeds) / n
    ts = sum(arms["staged"][s][1] * arms["staged"][s][0] for s in seeds) / n
    print(f"live   avg = {tl:.5f}")
    print(f"staged avg = {ts:.5f}")

    diffs = [arms["staged"][s][1] - arms["live"][s][1] for s in seeds]
    mean = sum(diffs) / len(diffs)
    sd = math.sqrt(sum((x - mean) ** 2 for x in diffs) / (len(diffs) - 1)) if len(diffs) > 1 else 0.0
    se = sd / math.sqrt(len(diffs)) if sd else 0.0
    t = mean / se if se else float("nan")
    print(f"delta (staged - live) = {ts - tl:+.5f}   [negative = staged BETTER]")
    print(f"paired: mean={mean:+.5f} sd={sd:.5f} se={se:.5f} t={t:+.2f}")
    better = sum(1 for x in diffs if x < -1e-9)
    worse = sum(1 for x in diffs if x > 1e-9)
    print(f"seeds staged-better {better}  staged-worse {worse}  tied {len(diffs) - better - worse}")
    if sd < 1e-12:
        print("!! ZERO VARIANCE across seeds -- the classic tell for replayed games; check seed spacing")
    same = sum(1 for s in seeds if arms["live"][s][2] == arms["staged"][s][2])
    print(f"identical digests: {same}/{len(seeds)}"
          + ("   (arms are byte-identical -- the staged model is not engaging)" if same == len(seeds) else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
