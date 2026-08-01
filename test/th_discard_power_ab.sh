#!/usr/bin/env bash
# HIGH-POWER A/B: Treasure Hunt cleanup-discard rung 1 (spare cards) vs rung 8 (full keep-order).
#
# The ladder sweep ran the suite's TH cases -- 6 searched cases and 3 d0 cases -- and could not
# separate the two rungs (searched -0.0073 vs +0.0040, d0 identical to four decimals). That is a
# POWER problem, not a result, so this runs the same comparison at ~20x the games.
#
# FRESH SEEDS on purpose: 8008..15015 are in none of the suite's three modes, so neither rung has
# been selected on them. Rung 1 was adopted off train+held-out; rung 8 came from domain reasoning
# rather than a fit; either way this set is clean for both.
#
# NEITHER ARM SEARCHES ANYTHING EXTRA. Both are pure orderings of the provider's candidate list --
# the rollout cleanup and the executor's tie-break each take index 0 -- so no plan variants and no
# extra rollouts are created, and the arms cannot differ in search cost. The separate rollout
# discard AXIS (MTG_TH_DISCARD_WIDTH) stays at its inert default of 1 in both arms.
#
# ONE pooled batch per arm (repo policy: one load-imbalance tail, not one per job).
#   bash test/th_discard_power_ab.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
OUT=logs/th_discard_power
mkdir -p "$OUT"

DECK=decks/treasure_hunt/treasure_hunt.txt
PROF=decks/treasure_hunt/treasure_hunt.profile.json
SEEDS="8008 9009 10010 11011 12012 13013 14014 15015"

build_manifest() {
    local out=$1
    {
        for s in $SEEDS; do
            # d0 carries most of the statistical power: no rollout, cheap, and it is the arm where
            # the heuristic decides ALONE (at searched depth the executor's rollout vetoes the
            # catastrophic discards, which compresses any difference between two sane rankings).
            h_job "th_d0_s$s"  "$DECK" "$PROF" 6000 "$s" depth=0 budget_ms=0 ignore_play_profile=true
            h_job "th_d3_s$s"  "$DECK" "$PROF" 1500 "$s" depth=3 budget_ms=10 ignore_play_profile=true
            h_job "th_d5_s$s"  "$DECK" "$PROF" 1000 "$s" depth=5 budget_ms=20 ignore_play_profile=true
        done
    } | h_manifest "$out"
}

build_manifest "$OUT/manifest.json" >/dev/null || exit 1
for v in 1 8; do
    echo "===== ARM rung=$v ($(date +%H:%M:%S)) ====="
    MTG_TH_DISCARD=$v h_batch "$BIN" "$OUT/manifest.json" "$OUT" "rung$v" >/dev/null
    echo "   ${H_BATCH_SECONDS}s"
done

python3 - "$OUT" "$SEEDS" <<'PY'
import sys, re, math, os
out, seeds = sys.argv[1], sys.argv[2].split()
def load(p):
    d = {}
    for ln in open(p):
        m = re.match(r'^(\S+): played=(\d+) avg=([0-9.]+)', ln)
        if m: d[m.group(1)] = (int(m.group(2)), float(m.group(3)))
    return d
a, b = load(f"{out}/rung1.log"), load(f"{out}/rung8.log")

print(f"\n{'depth':>6} {'games/arm':>10} {'rung1':>9} {'rung8':>9} {'delta':>9}  (negative = rung 8 better)")
grand_n = 0
for depth in ('d0', 'd3', 'd5'):
    jobs = [f"th_{depth}_s{s}" for s in seeds if f"th_{depth}_s{s}" in a and f"th_{depth}_s{s}" in b]
    if not jobs: continue
    n  = sum(a[j][0] for j in jobs)
    # games-weighted mean, so a depth is not distorted by unequal job sizes
    ma = sum(a[j][0]*a[j][1] for j in jobs) / n
    mb = sum(b[j][0]*b[j][1] for j in jobs) / n
    grand_n += n
    print(f"{depth:>6} {n:>10} {ma:9.4f} {mb:9.4f} {mb-ma:+9.4f}")

print(f"\nper-seed d0 (the highest-power arm), rung8 - rung1:")
diffs = []
for s in seeds:
    j = f"th_d0_s{s}"
    if j in a and j in b:
        diffs.append(b[j][1] - a[j][1])
        print(f"   s{s:<6} {a[j][1]:.4f} -> {b[j][1]:.4f}   {b[j][1]-a[j][1]:+.4f}")
if diffs:
    m  = sum(diffs)/len(diffs)
    sd = math.sqrt(sum((d-m)**2 for d in diffs)/(len(diffs)-1)) if len(diffs) > 1 else 0.0
    se = sd/math.sqrt(len(diffs)) if sd else 0.0
    print(f"\n   mean {m:+.4f}   sd {sd:.4f}   se {se:.4f}"
          + (f"   t={m/se:+.2f}" if se else "")
          + f"   ({sum(1 for d in diffs if d<0)}/{len(diffs)} seeds favour rung 8)")
    print(f"   |mean| vs se: {'SEPARATED' if se and abs(m) > 2*se else 'NOT separated -- the two rungs are indistinguishable'}")
PY
echo ALLDONE
