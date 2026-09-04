#!/usr/bin/env python3
"""Paired report for the EDF refloat A/B (see test/edf_refloat_ab.sh).

Reads the per-game `[win] job=... gi=... wt=...` lines MTG_DUMP_WINS writes to stderr, keys every
game by (seed, game_index) so the arms line up game-for-game, and reports the PAIRED difference.

Paired, not two independent means, and the distinction is not pedantic here: this deck's variance
across shuffles dwarfs the effect being measured, so an unpaired comparison of 800-game means would
be swamped by which openers each arm happened to draw. Same seeds + same game indices = same
shuffles, so the per-game difference cancels the shuffle entirely.

A game the engine never won scores max_turns+1 (9) -- the loss-penalized metric the whole repo uses
(test/check_gt_logs.py); win% is never reported.
"""
import collections
import math
import re
import sys

MAX_TURNS = 8
LINE = re.compile(r"\[win\] job=edf_(?P<arm>\w+?)_s(?P<seed>\d+)_g(?P<chunk>\d+) gi=(?P<gi>\d+) wt=(?P<wt>-?\d+)")


def score(wt):
    return wt if wt > 0 else MAX_TURNS + 1


def main(path):
    # arm -> (seed, gi) -> score.  The seed in the job NAME is the chunk's base seed; the pairing key
    # is (chunk-base seed - chunk offset, gi) == (run seed, gi), which is identical across arms.
    games = collections.defaultdict(dict)
    for line in open(path, errors="replace"):
        m = LINE.search(line)
        if not m:
            continue
        arm = m.group("arm")
        base_seed = int(m.group("seed")) - int(m.group("chunk"))
        games[arm][(base_seed, int(m.group("gi")))] = score(int(m.group("wt")))

    if not games:
        print("no [win] lines found -- was MTG_DUMP_WINS=1 set?", file=sys.stderr)
        return 1

    arms = sorted(games)
    print(f"{'arm':8s} {'games':>6s} {'mean':>8s}   distribution (win turn -> count, 9 = unwon)")
    for a in arms:
        v = list(games[a].values())
        dist = collections.Counter(v)
        ds = "  ".join(f"T{t}:{dist[t]}" for t in sorted(dist))
        print(f"{a:8s} {len(v):6d} {sum(v)/len(v):8.4f}   {ds}")

    print()
    print("paired differences (negative = the first arm wins FASTER, which is better):")
    for i, a in enumerate(arms):
        for b in arms[i + 1:]:
            keys = sorted(set(games[a]) & set(games[b]))
            if not keys:
                continue
            d = [games[a][k] - games[b][k] for k in keys]
            n = len(d)
            mean = sum(d) / n
            var = sum((x - mean) ** 2 for x in d) / (n - 1) if n > 1 else 0.0
            se = math.sqrt(var / n) if n > 1 else 0.0
            t = mean / se if se > 0 else 0.0
            # Per-seed sign, so a result carried by one lucky seed cannot hide inside the mean.
            byseed = collections.defaultdict(list)
            for k in keys:
                byseed[k[0]].append(games[a][k] - games[b][k])
            better = sum(1 for s in byseed if sum(byseed[s]) < 0)
            worse = sum(1 for s in byseed if sum(byseed[s]) > 0)
            changed = sum(1 for x in d if x != 0)
            print(f"  {a:6s} - {b:6s}  n={n:5d}  mean={mean:+.4f}  se={se:.4f}  t={t:+.2f}  "
                  f"seeds better/worse={better}/{worse}  games changed={changed}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
