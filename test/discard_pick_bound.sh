#!/usr/bin/env bash
# Does the cleanup-discard TIE-BREAK matter at all? A headroom BOUND, run before building any axis.
#
# Both halves of SelectCleanupDiscardIndex break their tie by HAND ORDER -- "first non-staged land",
# "first card at the top mana value". MTG_TRACE=discard measured that this tie-break is live in
# 100% of the discards Treasure Hunt actually makes (336 events / 400 d0 games), choosing among
# 4-22 lands that are genuinely DIFFERENT cards (13 distinct land names, all mana value 0: a
# Reliquary Tower, three cyclers, two storage lands, two scry/surveil lands, four duals).
#
# It is also almost exclusively a TH decision -- per 400 d0 games: th 336, hinata 36, dragonstorm 28,
# antilife 10, burn 3, auras 3, knights 1, slivers 0, goblins 0.
#
# MTG_DISCARD_PICK=last keeps the identical ranking and takes the OTHER end of the tied set. If
# first and last play the same, the decision does not matter and no search axis over it can repay
# its cost, whatever ranking would win -- the same bound MTG_LACKEY_RANK=low provided. If they
# differ materially, the decision is load-bearing and worth a real post-dedup axis.
#
# TRAIN seeds only (smoke 1001 + regression 2002/3003). Decks: the three where the rule actually
# fires, as ONE pooled batch per arm.
#   bash test/discard_pick_bound.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
OUT=logs/discard_pick_bound
mkdir -p "$OUT"
DECKS=th,hinata,dragonstorm

for arm in first last keeper; do
  for mode in smoke regression; do
    echo "===== ARM $arm mode=$mode ($(date +%H:%M:%S)) ====="
    env MTG_DISCARD_PICK="$arm" \
      bash test/regression.sh --"$mode" --deck="$DECKS" > "$OUT/run_${arm}_${mode}.txt" 2>&1
    if ! grep -q '^Result:' "$OUT/run_${arm}_${mode}.txt"; then
      echo "FATAL: arm $arm/$mode produced no result" >&2; tail -5 "$OUT/run_${arm}_${mode}.txt" >&2; exit 1
    fi
    cp "test/results/${mode}.env" "$OUT/env_${arm}_${mode}.env"
  done
done

python3 - "$OUT" <<'PY'
import sys, os, collections
out = sys.argv[1]
def load(p):
    d = {}
    for ln in open(p):
        ln = ln.strip()
        if '=' in ln:
            k, v = ln.split('=', 1); d[k] = v
    return d
arm = {}
for a in ('first', 'last', 'keeper'):
    d = {}
    for m in ('smoke', 'regression'):
        d.update(load(f"{out}/env_{a}_{m}.env"))
    arm[a] = d
keys = sorted(set(arm['first']) & set(arm['last']) & set(arm['keeper']))
for probe in ('last', 'keeper'):
    per = collections.defaultdict(float); tot = 0.0; moved = 0
    label = 'opposite end of the tie' if probe == 'last' else 'ADVERSARIAL: shed the best card'
    print(f"\n=== {probe} vs first  ({label}) ===")
    print(f"{'case':36s} {'first':>9} {probe:>9} {'delta':>9}")
    for k in keys:
        f, l = float(arm['first'][k].split('/')[0]), float(arm[probe][k].split('/')[0])
        per[k.split('_')[0]] += l - f; tot += l - f
        if abs(l - f) > 1e-9:
            moved += 1
            print(f"{k:36s} {f:9.4f} {l:9.4f} {l-f:+9.4f}")
    print(f"cases whose score MOVED at all: {moved}/{len(keys)}")
    for d in sorted(per): print(f"  -- {d:12s} {per[d]:+.4f}")
    print(f"  == TOTAL (|delta| is the HEADROOM BOUND) {tot:+.4f}")
PY
echo ALLDONE
