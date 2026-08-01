#!/usr/bin/env bash
# Per-deck tutor-axis WIDTH sweep on the TRAIN seeds (smoke 1001 + regression 2002/3003).
#
# The first width sweep (docs/design/searched-action-subdecisions.md) ran on the HELD-OUT overnight
# seeds and produced one global default, 6, deliberately NOT split per deck -- picking per-deck
# values off the holdout would have consumed it. This sweeps the same widths on the TRAIN seeds
# instead, so the per-deck picks are made on data that was never the validation set, and the holdout
# stays available for exactly one confirmation of the finished config.
#
# Only three suite decks contain a tutor at all (antilife: Idyllic + Enlightened; hinata: Gamble;
# goblins: Matron), so the arms run those three as ONE pooled batch per width per mode -- the full
# suite would spend most of its makespan on decks the width cannot reach.
#
#   bash test/tutor_width_sweep.sh [widths...]      (default: 1 2 3 4 6 8 12)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
OUT=logs/tutor_width_sweep
mkdir -p "$OUT"
DECKS=antilife,hinata,goblins
WIDTHS=("$@"); [ ${#WIDTHS[@]} -eq 0 ] && WIDTHS=(1 2 3 4 6 8 12)

for w in "${WIDTHS[@]}"; do
  for mode in smoke regression; do
    echo "===== ARM w=$w mode=$mode ($(date +%H:%M:%S)) ====="
    env MTG_TUTOR_WIDTH="$w" \
      bash test/regression.sh --"$mode" --deck="$DECKS" > "$OUT/run_w${w}_${mode}.txt" 2>&1
    # An arm that did not actually run would leave the PREVIOUS arm's results in place, and `cp`
    # would happily copy them -- a stale file reads downstream as a real measurement. (This is not
    # hypothetical: the first attempt passed `--regression`, which the harness rejected as an
    # unknown arg, and every regression arm silently re-copied the last good env.) Fail loudly.
    if ! grep -q '^Result:' "$OUT/run_w${w}_${mode}.txt"; then
      echo "FATAL: arm w=$w mode=$mode produced no result -- see $OUT/run_w${w}_${mode}.txt" >&2
      tail -5 "$OUT/run_w${w}_${mode}.txt" >&2
      exit 1
    fi
    cp "test/results/${mode}.env" "$OUT/env_w${w}_${mode}.env"
  done
done

python3 - "$OUT" "${WIDTHS[@]}" <<'PY'
import sys, os, collections
out, widths = sys.argv[1], sys.argv[2:]

def load(p):
    d = {}
    if not os.path.exists(p): return d
    for ln in open(p):
        ln = ln.strip()
        if '=' not in ln: continue
        k, v = ln.split('=', 1)
        d[k] = float(v.split('/')[0])
    return d

# Metric = SUM of avg-win-turn over the SEARCHED cases (d3/d5) only. d0 has no rollout to score a
# variant with, so an extra plan there is enumeration order picking a fixed rule, not a search.
per = collections.defaultdict(dict)   # width -> deck -> summed searched score
for w in widths:
    vals = {}
    for mode in ('smoke', 'regression'):
        vals.update(load(os.path.join(out, f"env_w{w}_{mode}.env")))
    for k, v in vals.items():
        deck = k.split('_')[0]
        if '_d0_' in k: continue
        per[w][deck] = per[w].get(deck, 0.0) + v

decks = sorted({d for w in per for d in per[w]})
base = widths[0]
print(f"\nTRAIN-seed searched-depth sum by deck (lower = better; delta vs w={base})\n")
print(f"{'width':>6} " + " ".join(f"{d:>22}" for d in decks) + f" {'TOTAL':>12}")
for w in widths:
    if w not in per: continue
    row, tot, dtot = [], 0.0, 0.0
    for d in decks:
        v  = per[w].get(d, float('nan'))
        b  = per[base].get(d, float('nan'))
        row.append(f"{v:10.4f} ({v-b:+8.4f})")
        tot += v; dtot += v - b
    print(f"{w:>6} " + " ".join(row) + f" {dtot:+12.4f}")

print("\nPer-deck argmin (TRAIN):")
for d in decks:
    cand = [(per[w].get(d, float('inf')), w) for w in widths if w in per]
    best = min(cand)
    print(f"  {d:12s} w={best[1]:>3}  {best[0]:.4f}   "
          + "  ".join(f"w{w}={per[w][d]:.4f}" for _, w in sorted(cand, key=lambda t: int(t[1])) if w in per))
PY
echo ALLDONE
