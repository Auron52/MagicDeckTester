#!/usr/bin/env bash
# Treasure Hunt cleanup-discard ranking: sweep the LADDER on the TRAIN seeds.
#
# The rungs are cumulative, so each one's contribution is separately attributable:
#   0 base    the historical rule -- first non-staged land in HAND ORDER
#   1 dup     + duplicate lands first; a duplicate Land's Edge (dead) ahead of any land;
#             retrace (Throes) available but ranked behind every ordinary land
#   2 tapped  + plain enters-tapped lands next (Temple, Thundering Falls), depletion lands spared
#   3 nodig   + cycling / sac-to-draw lands protected to the back, hardest when no Treasure Hunt
#             is in hand
#
# Rung 0 must be byte-identical to the pre-provider baseline -- that is the control.
# TH is the only deck this touches, so the arms run TH alone as one pooled batch.
#   bash test/th_discard_sweep.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
OUT=logs/th_discard_sweep
mkdir -p "$OUT"

for v in 0 1 2 3 4 5; do
  for mode in smoke regression; do
    echo "===== ARM v=$v mode=$mode ($(date +%H:%M:%S)) ====="
    env MTG_TH_DISCARD="$v" \
      bash test/regression.sh --"$mode" --deck=th > "$OUT/run_v${v}_${mode}.txt" 2>&1
    if ! grep -q '^Result:' "$OUT/run_v${v}_${mode}.txt"; then
      echo "FATAL: arm v=$v/$mode produced no result" >&2
      tail -5 "$OUT/run_v${v}_${mode}.txt" >&2; exit 1
    fi
    cp "test/results/${mode}.env" "$OUT/env_v${v}_${mode}.env"
  done
done

python3 - "$OUT" <<'PY'
import sys, os
out = sys.argv[1]
NAME = {'0': 'base (hand order)', '1': 'spare cards', '2': '+tapped (CONFLATED)', '3': '+protect diggers',
        '4': 'tapped-nondig (faithful)', '5': '+mono before dual'}
def load(p):
    d = {}
    for ln in open(p):
        ln = ln.strip()
        if '=' in ln:
            k, v = ln.split('=', 1); d[k] = v
    return d
arm = {}
for v in ('0', '1', '2', '3', '4', '5'):
    d = {}
    for m in ('smoke', 'regression'):
        d.update(load(f"{out}/env_v{v}_{m}.env"))
    arm[v] = d
keys = sorted(set.intersection(*[set(a) for a in arm.values()]))
searched = [k for k in keys if '_d0_' not in k]
d0       = [k for k in keys if '_d0_' in k]

def total(v, sel): return sum(float(arm[v][k].split('/')[0]) for k in sel)
print(f"\n{'rung':>4} {'':18} {'searched (d3/d5)':>18} {'delta':>9} {'d0':>10} {'delta':>9}")
for v in ('0', '1', '2', '3', '4', '5'):
    print(f"{v:>4} {NAME[v]:18} {total(v,searched):18.4f} {total(v,searched)-total('0',searched):+9.4f}"
          f" {total(v,d0):10.4f} {total(v,d0)-total('0',d0):+9.4f}")
print("\nper-case, best rung vs base:")
best = min(('1','2','3','4','5'), key=lambda v: total(v, searched) + total(v, d0))
print(f"  (best by searched+d0 sum: rung {best} = {NAME[best]})")
for k in keys:
    b, n = float(arm['0'][k].split('/')[0]), float(arm[best][k].split('/')[0])
    if abs(n - b) > 1e-9:
        print(f"  {k:34s} {b:8.4f} -> {n:8.4f}  {n-b:+.4f}")
PY
echo ALLDONE
