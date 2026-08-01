#!/usr/bin/env bash
# HIGH-POWER 2x2: the Land's Edge ranked pitch x the Treasure Hunt keep-order ranking.
#
# TWO changes are outstanding and they interact, so measuring either alone would be misleading:
#
#   MTG_LE_RANKED_PITCH  Land's Edge picks its ammunition by the provider's discard ranking
#                        instead of by hand order (cb93d2d, ships OFF).
#   MTG_TH_DISCARD=9     the full keep-order shopping list, Fiery Islet counted as untapped mana
#                        rather than as a dig slot (rung 9).
#
# They interact because rung 1 -- the adopted default -- only NAMES the spare cards (dead duplicate
# Land's Edge, retrace, duplicate land). Ordinary lands fall through it in hand order, so a ranked
# pitch on top of rung 1 is very nearly inert: there is no order for it to apply. Only a ranking
# that orders the whole land pool gives the outlet something to consume. Hence the 2x2:
#
#         MTG_TH_DISCARD=1        MTG_TH_DISCARD=9
#   LE=0  A  shipped baseline     C  rung 9 alone, re-measured (see below)
#   LE=1  B  near-inert control   D  the pairing under test
#
# Arm B IS THE INERT CONTROL and is load-bearing methodology, not filler: this repo has twice
# shipped a "probe" that could not change a decision and read the null as evidence. If B does not
# come back essentially identical to A, the model of what rung 1 names is wrong and nothing else
# in the table should be believed.
#
# WHY ARM C IS A RE-MEASUREMENT. The first rung-8/9 number was taken while the keep-set ranking had
# two bugs (982e521: keep priority banded by ROLE instead of by SLOT, so it shed the cyclers it
# meant to protect; and a "never shed Reliquary Tower" guard written as a magic number that stopped
# being the maximum once the scale widened). That measurement described the bugs, not the ranking.
#
# FRESH SEEDS, AND DELIBERATELY NOT 8008..15015. Rung 9 was authored by INSPECTING the games that
# diverged in the earlier power run, which ran on 8008..15015 -- scoring it there would be scoring
# it on the data it was fit to. 16016..23023 appear in no suite mode and in no prior sweep.
#
# NEITHER FLAG ADDS SEARCH. Both are pure orderings of an existing candidate list (the rollout
# cleanup and the executor tie-break each take index 0; Land's Edge takes the first `count`), so no
# arm creates a plan variant or an extra rollout and the arms cannot differ in search cost. The
# rollout discard AXIS (MTG_TH_DISCARD_WIDTH) stays at its inert default of 1 in all four.
#
#   bash test/le_pitch_power_ab.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
OUT=logs/le_pitch_power
mkdir -p "$OUT"

DECK=$(h_deck treasure_hunt) || exit 1
PROF=$(h_profile treasure_hunt) || exit 1
SEEDS="16016 17017 18018 19019 20020 21021 22022 23023"

{
    for s in $SEEDS; do
        # d0 carries most of the power: no rollout, cheap, and the arm where the ranking decides
        # ALONE. At searched depth the rollout vetoes the catastrophic pitches, which compresses
        # the gap between any two sane orderings -- but d3/d5 are what we actually ship, so they
        # are here to check the d0 result survives a search that can overrule it.
        # Weighted HARD toward d0. A d3 game costs ~10x a d0 game and a d5 game ~20x, while the
        # effect under test is rarer than either -- an even split spends ~85% of the wall clock on
        # the arms with the least power to see it. d3/d5 are kept only to check that whatever d0
        # says survives a search that can overrule it.
        h_job "th_d0_s$s" "$DECK" "$PROF" 40000 "$s" depth=0 budget_ms=0  ignore_play_profile=true
        h_job "th_d3_s$s" "$DECK" "$PROF"  1500 "$s" depth=3 budget_ms=10 ignore_play_profile=true
        h_job "th_d5_s$s" "$DECK" "$PROF"   800 "$s" depth=5 budget_ms=20 ignore_play_profile=true
    done
} | h_manifest "$OUT/manifest.json" >/dev/null || exit 1

# One pooled batch per ARM -- the only legitimate reason for more than one h_batch (an arm needs a
# process-level MTG_* env the manifest cannot express). Every game of every seed and depth for a
# given arm is in ONE queue, so each arm pays exactly one load-imbalance tail.
run_arm() {  # <label> <le> <rung> <tower-spare>
    echo "===== ARM $1  (LE_RANKED_PITCH=$2 TH_DISCARD=$3 TH_TOWER_SPARE=$4)  $(date +%H:%M:%S) ====="
    MTG_LE_RANKED_PITCH=$2 MTG_TH_DISCARD=$3 MTG_TH_TOWER_SPARE=$4 \
        h_batch "$BIN" "$OUT/manifest.json" "$OUT" "$1" >/dev/null
    grep -q '^th_d0_' "$OUT/$1.log" || { echo "ARM $1 PRODUCED NO RESULTS -- aborting"; exit 1; }
    echo "   ${H_BATCH_SECONDS}s"
}
#          label        LE rung tower
run_arm A_le0r1_t0 0 1 0     # the shipped baseline, unchanged
run_arm B_le1r1_t0 1 1 0     # inert control: rung 1 names no ordinary land, so the pitch has no order
run_arm C_le0r9_t0 0 9 0     # rung 9 alone, re-measured post-982e521
run_arm D_le1r9_t0 1 9 0     # ranked pitch x full ranking
run_arm E_le1r9_t1 1 9 1     # + the Tower-is-spare rule: the whole proposal
run_arm F_le0r1_t1 0 1 1     # the Tower rule ALONE on the shipped config (cleanup only, no pitch)

echo
echo "===== divergence vs the shipped baseline A, d0 ====="
# THE BOUND, not a footnote. MTG_TRACE=lepitch measured that only ~2% of Land's Edge pitches are
# even a strict subset of the lands in hand -- pitch them all and the set is the same set whatever
# order names it. So the honest question is not "is the delta significant" but "how many games
# COULD it have changed, and of those, how many did it help": a null over 160k games where only
# ~100 diverge is a bound on the effect, whereas a null read as "no effect" is the trap.
for arm in B_le1r1_t0 C_le0r9_t0 D_le1r9_t0 E_le1r9_t1 F_le0r1_t1; do
    h_wins_diff "$OUT/A_le0r1_t0" "$OUT/$arm" 'th_d0_*' >/dev/null
    printf '   %-12s diverged=%-6s faster=%-6s slower=%-6s (of %s d0 games)\n' \
           "$arm" "$((H_SLOWER+H_FASTER))" "$H_FASTER" "$H_SLOWER" \
           "$(awk -F'played=| ' '/^th_d0_/ {n+=$3} END {print n}' "$OUT/A_le0r1_t0.log")"
done

python3 - "$OUT" "$SEEDS" <<'PY'
import sys, re, math
out, seeds = sys.argv[1], sys.argv[2].split()
ARMS = [('A_le0r1_t0', 'A shipped'),   ('B_le1r1_t0', 'B LE=1 r1'),
        ('C_le0r9_t0', 'C r9'),        ('D_le1r9_t0', 'D LE=1 r9'),
        ('E_le1r9_t1', 'E D+tower'),   ('F_le0r1_t1', 'F tower only')]
BASE = 'A_le0r1_t0'

def load(p):
    d = {}
    for ln in open(p):
        m = re.match(r'^(\S+): played=(\d+) avg=([0-9.]+)', ln)
        if m: d[m.group(1)] = (int(m.group(2)), float(m.group(3)))
    return d
A = {k: load(f"{out}/{k}.log") for k, _ in ARMS}

def mean(arm, depth):
    """games-weighted mean over the seeds, so unequal job sizes cannot distort a depth"""
    jobs = [f"th_{depth}_s{s}" for s in seeds if f"th_{depth}_s{s}" in A[arm]]
    n = sum(A[arm][j][0] for j in jobs)
    return (n, sum(A[arm][j][0]*A[arm][j][1] for j in jobs)/n) if n else (0, float('nan'))

print("\n(avg win turn, loss-penalised -- LOWER IS BETTER; delta vs A, negative = better)\n")
hdr = f"{'depth':>6} {'games/arm':>10}" + "".join(f"{lbl:>20}" for _, lbl in ARMS)
print(hdr); print('-'*len(hdr))
for depth in ('d0', 'd3', 'd5'):
    n, base = mean(BASE, depth)
    if not n: continue
    row = f"{depth:>6} {n:>10}"
    for k, _ in ARMS:
        _, m = mean(k, depth)
        row += f"{m:>11.4f}{'' if k==BASE else f'({m-base:+.4f})':>9}"
    print(row)

# Per-seed d0 paired deltas: same seed = same shuffles, so the pairing removes almost all of the
# between-seed variance and the t is on the DIFFERENCES, which is where the power is.
print("\nper-seed d0, paired delta vs A:")
print(f"   {'seed':>8} " + "".join(f"{lbl:>14}" for _, lbl in ARMS[1:]))
diffs = {k: [] for k, _ in ARMS[1:]}
for s in seeds:
    j = f"th_d0_s{s}"
    if j not in A[BASE]: continue
    line = f"   {s:>8} "
    for k, _ in ARMS[1:]:
        if j in A[k]:
            d = A[k][j][1] - A[BASE][j][1]
            diffs[k].append(d); line += f"{d:>+14.4f}"
    print(line)

print()
for k, lbl in ARMS[1:]:
    v = diffs[k]
    if not v: continue
    m  = sum(v)/len(v)
    sd = math.sqrt(sum((x-m)**2 for x in v)/(len(v)-1)) if len(v) > 1 else 0.0
    se = sd/math.sqrt(len(v)) if sd else 0.0
    t  = m/se if se else 0.0
    verdict = ("SEPARATED" if se and abs(t) > 2 else
               "not separated" if se else "IDENTICAL (no game diverged)")
    print(f"   {lbl:<12} mean {m:+.4f}  se {se:.4f}  t {t:+6.2f}  "
          f"({sum(1 for x in v if x < 0)}/{len(v)} seeds better)  {verdict}")

print("\nisolated effects on d0 (games-weighted, all seeds pooled; negative = better):")
d0 = {k: mean(k, 'd0')[1] for k, _ in ARMS}
print(f"   ranked pitch, at rung 1     : {d0['B_le1r1_t0']-d0['A_le0r1_t0']:+.4f}   (expect ~0: "
      f"rung 1 gives it no order to apply)")
print(f"   ranked pitch, at rung 9     : {d0['D_le1r9_t0']-d0['C_le0r9_t0']:+.4f}   <- where it bites")
print(f"   rung 9, at LE=0             : {d0['C_le0r9_t0']-d0['A_le0r1_t0']:+.4f}")
print(f"   rung 9, at LE=1             : {d0['D_le1r9_t0']-d0['B_le1r1_t0']:+.4f}")
print(f"   pitch x ranking interaction : "
      f"{(d0['D_le1r9_t0']-d0['C_le0r9_t0'])-(d0['B_le1r1_t0']-d0['A_le0r1_t0']):+.4f}")
print(f"   tower-spare, alone          : {d0['F_le0r1_t1']-d0['A_le0r1_t0']:+.4f}   (cleanup only)")
print(f"   tower-spare, on D           : {d0['E_le1r9_t1']-d0['D_le1r9_t0']:+.4f}   (also the pitch)")
print(f"   FULL PROPOSAL E vs shipped  : {d0['E_le1r9_t1']-d0['A_le0r1_t0']:+.4f}")
PY
echo ALLDONE
