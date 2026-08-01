#!/usr/bin/env bash
# Is the plain-cantrip breakpoint class (MTG_BP_SITES bit 3) BUDGET-limited?
#
# It measures -0.0500 at unlimited budget and +0.0090 at the suite budget, which says "correct but
# unaffordable" -- but that is an inference, not a measurement. This makes it falsifiable: free
# budget somewhere ELSE (cap the tutor group, the measured 75% of Hinata's enumeration odometer --
# see docs/design/hinata-branching-root-cause.md) and watch the class delta. If the class is
# budget-limited its delta must move toward -0.0500 as the tutor cap tightens. If it does not move,
# the "unaffordable" reading is wrong and the limiter is elsewhere.
#
# Runs Hinata's OVERNIGHT (held-out) cases -- one pooled batch per arm, per repo policy.
# Widths run TIGHTEST-LAST: 8, 4, 2, then 1 == the axis off entirely (one target, the provider's).
# NOTE: MTG_TUTOR_WIDTH=0 does NOT mean "uncapped" -- it always clamped to 1, and now means "defer
# to the provider's own width". Arms are spelled explicitly so the label matches the arm.
#   bash test/cantrip_budget_ab.sh [widths...]      (default: 8 4 2 1)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
OUT=logs/cantrip_budget_ab
mkdir -p "$OUT"
WIDTHS=("$@"); [ ${#WIDTHS[@]} -eq 0 ] && WIDTHS=(8 4 2 1)

for w in "${WIDTHS[@]}"; do
  for sites in 0x17 31; do
    tag="w${w}_s${sites}"
    echo "===== ARM $tag (MTG_TUTOR_WIDTH=$w MTG_BP_SITES=$sites) ====="
    env MTG_TUTOR_WIDTH="$w" MTG_BP_SITES="$sites" \
      bash test/regression.sh --overnight --deck=hinata > "$OUT/run_$tag.txt" 2>&1
    cp test/results/overnight.env "$OUT/env_$tag.env"
  done
done

python3 - "$OUT" "${WIDTHS[@]}" <<'PY'
import sys, os
out, widths = sys.argv[1], sys.argv[2:]
def load(p):
    d = {}
    for ln in open(p):
        ln = ln.strip()
        if '=' not in ln: continue
        k, v = ln.split('=', 1)
        d[k] = float(v.split('/')[0])
    return d
print(f"\n{'tutor width':>12} {'class OFF':>10} {'class ON':>10} {'delta(ON-OFF)':>14}")
for w in widths:
    try:
        off = load(os.path.join(out, f"env_w{w}_s0x17.env"))
        on  = load(os.path.join(out, f"env_w{w}_s31.env"))
    except OSError:
        continue
    keys = sorted(set(off) & set(on))
    # sum over cases: the suite fingerprint is per-case avg win turn, equally weighted (as the
    # existing cantrip measurements were reported)
    so, sn = sum(off[k] for k in keys), sum(on[k] for k in keys)
    label = "uncapped" if w == "0" else w
    print(f"{label:>12} {so/len(keys):10.4f} {sn/len(keys):10.4f} {(sn-so)/len(keys):+14.4f}")
    for k in keys:
        if abs(on[k]-off[k]) > 1e-9:
            print(f"{'':>12}   {k:34s} {off[k]:.4f} -> {on[k]:.4f}  {on[k]-off[k]:+.4f}")
PY
echo ALLDONE
