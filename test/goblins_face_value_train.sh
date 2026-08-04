#!/usr/bin/env bash
# Train the face-damage weight (MTG_GOBLIN_FACE_VALUE_PER) on a TRAIN split, then read the winner off
# a VALIDATION split that was never used to choose it.
#
# Why not just sweep the overnight tier and take the best: that IS the holdout, so picking its minimum
# is selection on the validation set and the reported gain is a winner's-curse. The regression tier is
# the natural train set but its ~1,325 searched games cannot resolve a delta this small (the lesson
# recorded in docs/design/goblins-enabler-worse-games.md, which cost a wrong verdict once already).
#
# So split the overnight searched cases by SEED, which keeps both halves large:
#     TRAIN      s4004 + s5005   (d3 + d5, 4,000 searched games)
#     VALIDATE   s6006 + s7007   (d3 + d5, 4,000 searched games)
# plus smoke + regression reported alongside as extra train signal. Selection uses TRAIN ONLY; the
# validation column is printed for every weight purely so the winner's-curse is visible rather than
# hidden -- do not pick on it.
#
# d0 is reported but never selected on: the user's standing priority is that searched is what ships
# and d0 is a diagnostic (docs/design/goblins-enabler-worse-games.md).
#
# Usage:  bash test/goblins_face_value_train.sh [weights...]      # default 0 80 100 120 140 160 180 200
set -uo pipefail
cd "$(dirname "$0")/.."

WEIGHTS="${*:-0 80 100 120 140 160 180 200}"
printf "%-8s | %-22s | %-22s | %s\n" "per" "TRAIN (s4004+s5005)" "VALIDATE (s6006+s7007)" "d0 (all seeds)"
printf -- "---------+------------------------+------------------------+----------------\n"

for w in $WEIGHTS; do
  if [ "$w" = "0" ]; then
    env MTG_GOBLIN_FACE_VALUE=0 bash test/regression.sh --overnight --deck=goblins >/dev/null 2>&1
  else
    env MTG_GOBLIN_FACE_VALUE=1 MTG_GOBLIN_FACE_VALUE_PER="$w" \
        bash test/regression.sh --overnight --deck=goblins >/dev/null 2>&1
  fi
  W="$w" python3 - <<'PY'
import glob, os
gt, new = "test/gt_logs", "test/logs/overnight/wins"
pen = lambda v: v if v > 0 else 99
load = lambda p: {int(t[0]): int(t[1]) for t in (l.split() for l in open(p)) if len(t) >= 2}
TRAIN, VALID = ("_s4004", "_s5005"), ("_s6006", "_s7007")
acc = {"train": [0, 0], "valid": [0, 0], "d0": [0, 0]}   # [turn-units, games]
for f in sorted(glob.glob(f"{new}/goblins_overnight_*.wins")):
    g = os.path.join(gt, os.path.basename(f))
    if not os.path.exists(g):
        continue
    a, c = load(g), load(f)
    common = [i for i in a if i in c]
    d = sum(pen(c[i]) - pen(a[i]) for i in common)
    if "_d0_" in f:
        key = "d0"
    elif any(s in f for s in TRAIN):
        key = "train"
    elif any(s in f for s in VALID):
        key = "valid"
    else:
        continue
    acc[key][0] += d
    acc[key][1] += len(common)
fmt = lambda k: f"{acc[k][0]:+5d} tu / {acc[k][1]:5d}g"
print(f"{os.environ['W']:<8} | {fmt('train'):<22} | {fmt('valid'):<22} | {fmt('d0')}")
PY
done
