#!/usr/bin/env bash
# What is the WHOLE Treasure Hunt discard ladder worth, measured against the arbitrary rule?
#
# Every delta so far stacked against an already-improved baseline: rung 9 was measured vs rung 1
# (the adopted spare-cards rule), which isolates the NEW rule correctly but never answers "what did
# the ladder buy in total". Rung 0 is the arbitrary rule -- the deck-agnostic base ranking, whose
# tier A says "shed a land" and then picks WHICH by hand order.
#
# Same manifest, same seeds (16016..23023) and same job sizes as test/le_pitch_power_ab.sh, so
# G/A' here are directly comparable to the A..F arms already in logs/le_pitch_power.
#
# A' RE-RUNS THE SHIPPED BASELINE ON PURPOSE. The binary was rebuilt between that run and this one
# (a g_le_pitch_ranking diagnostic guard and card names in the lepitch trace). Both changes are
# supposed to be trace-only, and "supposed to be" is not a measurement -- if A' does not reproduce
# A to four decimals, the rebuild was not neutral and the cross-run comparison is void.
#
#   bash test/th_rung0_baseline_ab.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
OUT=logs/le_pitch_power
[ -f "$OUT/manifest.json" ] || { echo "no manifest -- run test/le_pitch_power_ab.sh first" >&2; exit 1; }

run_arm() {  # <label> <le> <rung> <tower>
    echo "===== ARM $1  (LE_RANKED_PITCH=$2 TH_DISCARD=$3 TH_TOWER_SPARE=$4)  $(date +%H:%M:%S) ====="
    MTG_LE_RANKED_PITCH=$2 MTG_TH_DISCARD=$3 MTG_TH_TOWER_SPARE=$4 \
        h_batch "$BIN" "$OUT/manifest.json" "$OUT" "$1" >/dev/null
    grep -q '^th_d0_' "$OUT/$1.log" || { echo "ARM $1 PRODUCED NO RESULTS -- aborting"; exit 1; }
    echo "   ${H_BATCH_SECONDS}s"
}
run_arm G_le0r0_t0 0 0 0    # the arbitrary rule: base ranking, land chosen by hand order
run_arm A2_le0r1_t0 0 1 0   # re-run of the shipped baseline == rebuild-neutrality control

python3 - "$OUT" <<'PY'
import re, sys, math
out = sys.argv[1]
seeds = "16016 17017 18018 19019 20020 21021 22022 23023".split()
def load(p):
    d = {}
    try: f = open(p)
    except OSError: return d
    for ln in f:
        m = re.match(r'^(\S+): played=(\d+) avg=([0-9.]+)', ln)
        if m: d[m.group(1)] = (int(m.group(2)), float(m.group(3)))
    return d
ARMS = [('G_le0r0_t0','G rung0 (arbitrary)'), ('A_le0r1_t0','A rung1 (shipped)'),
        ('A2_le0r1_t0','A2 rung1 re-run'),    ('C_le0r9_t0','C rung9'),
        ('D_le1r9_t0','D rung9+pitch'),       ('E_le1r9_t1','E +tower')]
A = {k: load(f"{out}/{k}.log") for k, _ in ARMS}

def mean(arm, depth):
    jobs = [f"th_{depth}_s{s}" for s in seeds if f"th_{depth}_s{s}" in A.get(arm, {})]
    n = sum(A[arm][j][0] for j in jobs)
    return (n, sum(A[arm][j][0]*A[arm][j][1] for j in jobs)/n) if n else (0, float('nan'))

print("\n=== REBUILD-NEUTRALITY CONTROL ===")
for depth in ('d0','d3','d5'):
    _, a  = mean('A_le0r1_t0', depth)
    _, a2 = mean('A2_le0r1_t0', depth)
    ok = 'OK' if abs(a-a2) < 5e-5 else '*** MISMATCH -- cross-run comparison is VOID ***'
    print(f"   {depth}  A={a:.4f}  A2={a2:.4f}  diff={a2-a:+.4f}  {ok}")

print("\n=== THE LADDER, measured against the ARBITRARY rule (rung 0) ===")
print("   (avg win turn, loss-penalised -- LOWER IS BETTER)\n")
print(f"   {'arm':<22}{'d0':>10}{'vs rung0':>11}{'d3':>10}{'vs rung0':>11}{'d5':>10}{'vs rung0':>11}")
base = {d: mean('G_le0r0_t0', d)[1] for d in ('d0','d3','d5')}
for k, lbl in ARMS:
    if not A.get(k): continue
    row = f"   {lbl:<22}"
    for d in ('d0','d3','d5'):
        _, m = mean(k, d)
        row += f"{m:>10.4f}" + (f"{m-base[d]:>+11.4f}" if k != 'G_le0r0_t0' else f"{'--':>11}")
    print(row)

print("\n   per-seed d0, paired vs rung 0:")
for k, lbl in ARMS[1:]:
    if not A.get(k): continue
    v = [A[k][f"th_d0_s{s}"][1] - A['G_le0r0_t0'][f"th_d0_s{s}"][1]
         for s in seeds if f"th_d0_s{s}" in A[k] and f"th_d0_s{s}" in A['G_le0r0_t0']]
    if not v: continue
    m  = sum(v)/len(v)
    sd = math.sqrt(sum((x-m)**2 for x in v)/(len(v)-1)) if len(v) > 1 else 0.0
    se = sd/math.sqrt(len(v)) if sd else 0.0
    print(f"   {lbl:<22} mean {m:+.4f}  se {se:.4f}"
          + (f"  t {m/se:+6.2f}" if se else "  t     --")
          + f"  ({sum(1 for x in v if x < 0)}/{len(v)} seeds better)")
PY
echo ALLDONE
