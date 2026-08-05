#!/usr/bin/env bash
# Train the Skirk Prospector per-body ramp rate (MTG_GOBLIN_SKIRK_RATE, hundredths) on a TRAIN split,
# then read the winner off a VALIDATION split that was never used to choose it.
#
# Same protocol and the same reason as test/goblins_face_value_train.sh: sweeping the whole overnight
# tier and taking its minimum is selection on the holdout, and the regression tier's ~1,325 searched
# games cannot resolve a delta this small -- a limit that has already produced one wrong verdict in
# docs/design/goblins-enabler-worse-games.md (the Piledriver crowd arm measured -4.0 on regression and
# did not replicate at all held-out).
#
#     TRAIN      s4004 + s5005   (d3 + d5, 4,000 searched games)
#     VALIDATE   s6006 + s7007   (d3 + d5, 4,000 searched games)
#
# Selection uses TRAIN ONLY; the validation column is printed for every rate purely so the
# winner's-curse is visible rather than hidden -- do not pick on it.
#
# Why the rate and not the cap: at the states where Skirk is committed past the window the cap is not
# what binds. s3003 gi194 credits 0.26 = 0.13 x 2, because its single board Goblin is a lord (so
# goblin_fodder is 0) and only the entering Matron and the Prospector itself are fodder. Raising the
# 0.40 cap would change nothing there; the per-body rate is the lever.
#
# The cap is swept alongside so a higher rate cannot silently clip on the high-fodder boards.
#
# Usage:  bash test/goblins_skirk_rate_train.sh [rates...]        # default 13 18 22 26 32
#         SKIRK_CAP=60 bash test/goblins_skirk_rate_train.sh
set -uo pipefail
cd "$(dirname "$0")/.."

RATES="${*:-13 18 22 26 32}"
CAP="${SKIRK_CAP:-60}"
printf "%-6s | %-22s | %-22s | %s\n" "rate" "TRAIN (s4004+s5005)" "VALIDATE (s6006+s7007)" "d0 (all seeds)"
printf -- "-------+------------------------+------------------------+----------------\n"

for r in $RATES; do
  env MTG_GOBLIN_SKIRK_RATE="$r" MTG_GOBLIN_SKIRK_CAP="$CAP" \
      bash test/regression.sh --overnight --deck=goblins >/dev/null 2>&1
  R="$r" python3 - <<'PY'
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
print(f"{os.environ['R']:<6} | {fmt('train'):<22} | {fmt('valid'):<22} | {fmt('d0')}")
PY
done
