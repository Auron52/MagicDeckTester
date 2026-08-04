#!/usr/bin/env python3
"""How many GAMES does the Goblins tutor width actually decide?

Turn-units answer "how much"; they never answer "how many games, and which". This runs a set of
widths against committed ground truth (which is the shipped W=6) and reports, for the SEARCHED tiers,
the distinct games each width wins or loses -- deduplicated across d3/d5, since the same game appears
in both and would otherwise be double counted.

Two questions it exists to answer (user, 2026-08-04):
  * how many games would we save / lose by changing the width?
  * how many games are still OUTSTANDING at the shipped width -- i.e. how many a wider axis would
    still recover, which is the size of the remaining prize.

Usage:  python3 test/goblins_width_games.py 4 12 16
"""
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODES = ("smoke", "regression", "overnight")
pen = lambda v: v if v > 0 else 99


def load(p):
    return {int(t[0]): int(t[1]) for t in (l.split() for l in open(p)) if len(t) >= 2}


def measure(width):
    """{(seed, gi): (gt_turn, new_turn)} for searched cases where the width changed the outcome."""
    better, worse = {}, {}
    for mode in MODES:
        env = dict(os.environ, MTG_TUTOR_WIDTH=str(width))
        subprocess.run(["bash", "test/regression.sh", f"--{mode}", "--deck=goblins"],
                       cwd=ROOT, capture_output=True, text=True, env=env)
        for f in sorted(glob.glob(os.path.join(ROOT, f"test/logs/{mode}/wins/goblins_{mode}_d[35]_*.wins"))):
            base = os.path.basename(f)
            g = os.path.join(ROOT, "test/gt_logs", base)
            if not os.path.exists(g):
                continue
            seed = re.search(r"_s(\d+)\.wins$", base).group(1)
            gt, new = load(g), load(f)
            for i in gt:
                if i not in new:
                    continue
                if pen(new[i]) < pen(gt[i]):
                    better[(seed, i)] = (gt[i], new[i])
                elif pen(new[i]) > pen(gt[i]):
                    worse[(seed, i)] = (gt[i], new[i])
    return better, worse


def main():
    widths = [int(a) for a in sys.argv[1:]] or [4, 12, 16]
    print("vs the shipped W=6 ground truth; searched depths only; distinct games (d3/d5 deduped)\n")
    for w in widths:
        better, worse = measure(w)
        tag = "saves" if w > 6 else "would lose"
        print(f"=== W={w} ===")
        print(f"  games BETTER than W=6 : {len(better):3d}")
        print(f"  games WORSE  than W=6 : {len(worse):3d}")
        for label, d in (("better", better), ("worse", worse)):
            if d:
                print(f"    {label}: " + ", ".join(
                    f"s{s} gi{i} T{a}->T{b}" for (s, i), (a, b) in sorted(d.items())))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
