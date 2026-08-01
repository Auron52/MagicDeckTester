#!/usr/bin/env bash
# A/B: prune casting a duplicate legend that does nothing on entry (MTG_PRUNE_DUP_LEGEND).
#
# Hinata, Dawn-Crowned is a 4-mana legendary with NO enter effect (vanilla_creature +
# hinata_cost_reducer, a continuous discount). The legend rule kills a second copy the instant it
# resolves, so casting one buys nothing and costs a card plus that turn's mana. MTG_TRACE=legend
# measured it happening 109 times in 600 real d0 games; with the prune on, 0.
#
# It is a PRUNE, so the expected gain is of two kinds and they are worth separating:
#   1. the wasted mana/card itself, and
#   2. rollout budget -- plan variants share a FIXED budget, so a variant that cannot be better
#      dilutes every real one. That is the mechanism that made the rollout discard axis lose.
#
# Hinata is the only deck with an enter-inert legend that duplicates, so the other decks are here
# only to confirm the prune is inert for them (it must be: the whitelist is positive, and Muxus --
# the one duplicate legend that DOES have an enter effect -- is `custom` and never pruned).
#
#   bash test/dup_legend_prune_ab.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
OUT=logs/dup_legend
mkdir -p "$OUT"

SEEDS="41041 42042 43043 44044 45045 46046"      # fresh
{
    for s in $SEEDS; do
        h_job "hinata_d0_s$s" "$(h_deck Hinata2)" "$(h_profile Hinata2)" 6000 "$s" \
              depth=0 budget_ms=0  ignore_play_profile=true
        h_job "hinata_d3_s$s" "$(h_deck Hinata2)" "$(h_profile Hinata2)"  400 "$s" \
              depth=3 budget_ms=10 ignore_play_profile=true
        h_job "hinata_d5_s$s" "$(h_deck Hinata2)" "$(h_profile Hinata2)"  200 "$s" \
              depth=5 budget_ms=20 ignore_play_profile=true
    done
    # inertness controls: goblins duplicates Muxus (has an enter effect -> must NOT be pruned),
    # knights duplicates Haytham (LordEffect -> IS pruned, so it is a second live case).
    for s in $SEEDS; do
        h_job "goblins_d0_s$s" "$(h_deck goblins)" "$(h_profile goblins)" 2000 "$s" \
              depth=0 budget_ms=0 ignore_play_profile=true
        h_job "knights_d0_s$s" "$(h_deck knights)" "$(h_profile knights)" 2000 "$s" \
              depth=0 budget_ms=0 ignore_play_profile=true
    done
} | h_manifest "$OUT/manifest.json" >/dev/null || exit 1

for v in 0 1; do
    echo "===== ARM MTG_PRUNE_DUP_LEGEND=$v  ($(date +%H:%M:%S)) ====="
    MTG_PRUNE_DUP_LEGEND=$v h_batch "$BIN" "$OUT/manifest.json" "$OUT" "p$v" >/dev/null
    grep -q '^hinata_d0_' "$OUT/p$v.log" || { echo "ARM $v PRODUCED NO RESULTS -- aborting"; exit 1; }
    echo "   ${H_BATCH_SECONDS}s"
done

python3 - "$OUT" "$SEEDS" <<'PY'
import sys, re, math
out, seeds = sys.argv[1], sys.argv[2].split()
def load(p):
    d = {}
    for ln in open(p):
        m = re.match(r'^(\S+): played=(\d+) avg=([0-9.]+)', ln)
        if m: d[m.group(1)] = (int(m.group(2)), float(m.group(3)))
    return d
a, b = load(f"{out}/p0.log"), load(f"{out}/p1.log")
print("\n(avg win turn, loss-penalised -- LOWER IS BETTER; negative = prune better)\n")
print(f"{'case':>16} {'games/arm':>10} {'no-prune':>10} {'prune':>10} {'delta':>10}")
for case in ('hinata_d0','hinata_d3','hinata_d5','goblins_d0','knights_d0'):
    jobs = [f"{case}_s{s}" for s in seeds if f"{case}_s{s}" in a and f"{case}_s{s}" in b]
    if not jobs: continue
    n  = sum(a[j][0] for j in jobs)
    ma = sum(a[j][0]*a[j][1] for j in jobs)/n
    mb = sum(b[j][0]*b[j][1] for j in jobs)/n
    tag = ''
    if case.startswith('goblins'):
        tag = '   <- MUST be 0.0000 (Muxus has an enter effect; pruning it would be a BUG)'
    print(f"{case:>16} {n:>10} {ma:>10.4f} {mb:>10.4f} {mb-ma:>+10.4f}{tag}")

for case in ('hinata_d0','knights_d0'):
    v = [b[f"{case}_s{s}"][1] - a[f"{case}_s{s}"][1] for s in seeds
         if f"{case}_s{s}" in a and f"{case}_s{s}" in b]
    if not v: continue
    m  = sum(v)/len(v)
    sd = math.sqrt(sum((x-m)**2 for x in v)/(len(v)-1)) if len(v) > 1 else 0.0
    se = sd/math.sqrt(len(v)) if sd else 0.0
    print(f"\n{case} per-seed: mean {m:+.4f}  se {se:.4f}"
          + (f"  t {m/se:+.2f}" if se else "  t --")
          + f"  ({sum(1 for x in v if x < 0)}/{len(v)} seeds better)")
PY
echo ALLDONE
