#!/usr/bin/env bash
# A/B: should Throes of Chaos' retrace cost pick its land by the deck's discard ranking?
#
# The base rule is `return hand_land_indices` -- the first land in hand order, the same
# arbitrary-ranking defect the cleanup shed and the Land's Edge pitch already shed. This asks the
# ranking that those two now use.
#
# WHY THIS ONE LOOKS MORE PROMISING THAN THE PITCH (measured, MTG_TRACE=retrace, 3000 d0 games):
#     events=348   contested (2+ lands)=341 (98%)   ranking moved the pick=209 (60%)
# The Land's Edge pitch, by contrast, burns the WHOLE hand 97.6% of the time, so its order is
# unobservable and only ~1.7% of activations differ. Retrace is a genuine one-of-N choice almost
# every time it happens -- ~0.070 moved picks per game against the pitch's ~0.013, about 5x the
# effective decision rate. Rare event, but a real decision when it fires.
#
# NEITHER ARM SEARCHES ANYTHING EXTRA: both are orderings of the same candidate list and the caller
# takes index 0, so no plan variants and no extra rollouts. Sized for a modest daytime footprint
# (~5 min total); scale the d0 count if the result lands inside the noise.
#
#   bash test/th_retrace_ab.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
OUT=logs/th_retrace
mkdir -p "$OUT"

DECK=$(h_deck treasure_hunt) || exit 1
PROF=$(h_profile treasure_hunt) || exit 1
SEEDS="32032 33033 34034 35035 36036 37037 38038 39039"   # fresh: unused by any suite mode or prior sweep

{
    for s in $SEEDS; do
        h_job "th_d0_s$s" "$DECK" "$PROF" 12000 "$s" depth=0 budget_ms=0  ignore_play_profile=true
        h_job "th_d3_s$s" "$DECK" "$PROF"  1000 "$s" depth=3 budget_ms=10 ignore_play_profile=true
        h_job "th_d5_s$s" "$DECK" "$PROF"   500 "$s" depth=5 budget_ms=20 ignore_play_profile=true
    done
} | h_manifest "$OUT/manifest.json" >/dev/null || exit 1

for v in 0 1; do
    echo "===== ARM MTG_TH_RETRACE_RANKED=$v  ($(date +%H:%M:%S)) ====="
    MTG_TH_RETRACE_RANKED=$v h_batch "$BIN" "$OUT/manifest.json" "$OUT" "r$v" >/dev/null
    grep -q '^th_d0_' "$OUT/r$v.log" || { echo "ARM $v PRODUCED NO RESULTS -- aborting"; exit 1; }
    echo "   ${H_BATCH_SECONDS}s"
done

echo
echo "===== divergence (d0) ====="
h_wins_diff "$OUT/r0" "$OUT/r1" 'th_d0_*' >/dev/null
printf '   diverged=%s  faster=%s  slower=%s  (of %s d0 games)\n' \
       "$((H_SLOWER+H_FASTER))" "$H_FASTER" "$H_SLOWER" \
       "$(awk -F'played=| ' '/^th_d0_/ {n+=$3} END {print n}' "$OUT/r0.log")"

python3 - "$OUT" "$SEEDS" <<'PY'
import sys, re, math
out, seeds = sys.argv[1], sys.argv[2].split()
def load(p):
    d = {}
    for ln in open(p):
        m = re.match(r'^(\S+): played=(\d+) avg=([0-9.]+)', ln)
        if m: d[m.group(1)] = (int(m.group(2)), float(m.group(3)))
    return d
a, b = load(f"{out}/r0.log"), load(f"{out}/r1.log")
print("\n(avg win turn, loss-penalised -- LOWER IS BETTER; negative delta = ranking better)\n")
print(f"{'depth':>6} {'games/arm':>10} {'hand-order':>12} {'ranked':>10} {'delta':>10}")
for depth in ('d0','d3','d5'):
    jobs = [f"th_{depth}_s{s}" for s in seeds if f"th_{depth}_s{s}" in a and f"th_{depth}_s{s}" in b]
    if not jobs: continue
    n  = sum(a[j][0] for j in jobs)
    ma = sum(a[j][0]*a[j][1] for j in jobs)/n
    mb = sum(b[j][0]*b[j][1] for j in jobs)/n
    print(f"{depth:>6} {n:>10} {ma:>12.4f} {mb:>10.4f} {mb-ma:>+10.4f}")

v = [b[f"th_d0_s{s}"][1] - a[f"th_d0_s{s}"][1] for s in seeds
     if f"th_d0_s{s}" in a and f"th_d0_s{s}" in b]
if v:
    m  = sum(v)/len(v)
    sd = math.sqrt(sum((x-m)**2 for x in v)/(len(v)-1)) if len(v) > 1 else 0.0
    se = sd/math.sqrt(len(v)) if sd else 0.0
    print(f"\nper-seed d0: mean {m:+.4f}  se {se:.4f}"
          + (f"  t {m/se:+.2f}" if se else "  t --")
          + f"  ({sum(1 for x in v if x < 0)}/{len(v)} seeds better)")
    print("  " + ("SEPARATED" if se and abs(m) > 2*se else "not separated at this sample size"))
PY
echo ALLDONE
