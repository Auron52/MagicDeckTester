#!/usr/bin/env bash
# Goblins tutor-ranking A/B: run one ARM (a set of MTG_* flags) over the Goblins cases of every tier
# and report the net loss-penalized turn-unit delta against committed ground truth, split d0 vs
# searched.
#
# Why split: the user's priority (2026-08-03) is "I'm less worried about the d0 problem than I am
# about the searched problems", and a combined net is dominated by d0's 8,000 greedy games. Variant 3
# was rejected on the wrong evidence exactly once because of that -- see
# docs/design/goblins-enabler-worse-games.md.
#
# Why per-tier: smoke + regression together are only ~1,325 searched games, enough to kill an
# obviously-bad arm fast and never enough to ACCEPT one. The overnight tier's 8,000 held-out searched
# games are the gate. Both are printed so a train/held-out disagreement is visible rather than
# averaged away.
#
# Both arms are the SAME binary -- the arm is env flags -- so there is no two-different-builds risk.
# Run the empty arm first: it must come back all-zero, which is the validity check that the flags are
# the only difference (regression-testing skill, rule 6).
#
# Usage:
#   bash test/goblins_rank_ab.sh                              # baseline (no flags) -- must be all 0
#   bash test/goblins_rank_ab.sh MTG_GOBLIN_LORD_AMP=1
#   bash test/goblins_rank_ab.sh MTG_GOBLIN_RANK_V2=1 MTG_GOBLIN_LORD_AMP=1
#   MODES="smoke overnight" bash test/goblins_rank_ab.sh ...  # subset of tiers
set -uo pipefail
cd "$(dirname "$0")/.."

MODES="${MODES:-smoke regression overnight}"
ARM="$*"
echo "=== arm: ${ARM:-<baseline>} ==="

for mode in $MODES; do
  env $ARM bash test/regression.sh "--$mode" --deck=goblins >/dev/null 2>&1
  ARM="$ARM" MODE="$mode" python3 - <<'PY'
import glob, os
mode = os.environ["MODE"]
gt, new = "test/gt_logs", f"test/logs/{mode}/wins"
pen = lambda v: v if v > 0 else 99          # unwon = horizon cutoff, the maximal slowdown
tot = {"d0": 0, "searched": 0}
games = {"d0": 0, "searched": 0}
moved = []
for f in sorted(glob.glob(f"{new}/goblins_{mode}_*.wins")):
    g = os.path.join(gt, os.path.basename(f))
    if not os.path.exists(g):
        continue
    load = lambda p: {int(t[0]): int(t[1]) for t in (l.split() for l in open(p)) if len(t) >= 2}
    a, c = load(g), load(f)
    tag = "d0" if "_d0_" in f else "searched"
    d = sum(pen(c[i]) - pen(a[i]) for i in a if i in c)
    tot[tag] += d
    games[tag] += len(a)
    worse = [i for i in a if i in c and pen(c[i]) > pen(a[i])]
    better = [i for i in a if i in c and pen(c[i]) < pen(a[i])]
    if worse or better:
        moved.append(f"    {os.path.basename(f)[:-5]:34s} {d:+5d}  better={len(better):3d} worse={len(worse):3d}")
for tag in ("searched", "d0"):
    n = games[tag] or 1
    print(f"  {mode:<11}{tag:<9} {tot[tag]:+6d} turn-units / {games[tag]:5d} games ({tot[tag]/n:+.4f}/game)")
print("\n".join(moved))
PY
done
