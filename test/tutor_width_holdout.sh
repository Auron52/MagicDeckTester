#!/usr/bin/env bash
# HELD-OUT confirmation of the provider-owned tutor widths (antilife 2 / hinata 2 / goblins 12).
#
# This is the ONE use of the overnight seeds for this change. The per-deck widths were chosen on the
# TRAIN seeds (test/tutor_width_sweep.sh, seeds 1001/2002/3003) precisely so these seeds could stay a
# validation set rather than a selection set -- so this script must be run ONCE, and its job is to
# answer "does the train-derived config hold up", not "which width is best".
#
#   arm OLD  MTG_TUTOR_WIDTH=6   the shipped global constant, every deck
#   arm NEW  (env unset)         each provider's own TutorSearchWidth()
#
# Both arms run the three tutor decks as ONE pooled batch each.
#   bash test/tutor_width_holdout.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
OUT=logs/tutor_width_holdout
mkdir -p "$OUT"
DECKS=antilife,hinata,goblins

run_arm() {   # $1=tag  $2...=env assignments
  local tag="$1"; shift
  echo "===== ARM $tag ($* ) $(date +%H:%M:%S) ====="
  env "$@" bash test/regression.sh --overnight --deck="$DECKS" > "$OUT/run_$tag.txt" 2>&1
  if ! grep -q '^Result:' "$OUT/run_$tag.txt"; then
    echo "FATAL: arm $tag produced no result -- see $OUT/run_$tag.txt" >&2
    tail -5 "$OUT/run_$tag.txt" >&2
    exit 1
  fi
  cp test/results/overnight.env "$OUT/env_$tag.env"
}

run_arm old MTG_TUTOR_WIDTH=6
run_arm new MTG_TUTOR_AXIS=1        # a no-op assignment: the point is that WIDTH stays unset

python3 - "$OUT" <<'PY'
import sys, os, collections
out = sys.argv[1]

def load(p):
    d = {}
    for ln in open(p):
        ln = ln.strip()
        if '=' not in ln: continue
        k, v = ln.split('=', 1)
        d[k] = v
    return d

old, new = load(f"{out}/env_old.env"), load(f"{out}/env_new.env")
keys = sorted(set(old) & set(new))

# Report searched (d3/d5) and d0 separately: the axis is depth-gated in EFFECT -- at d0 there is no
# rollout to score a variant with -- so mixing them would hide which one moved.
for label, want_d0 in (("SEARCHED (d3/d5)", False), ("d0", True)):
    sel = [k for k in keys if ('_d0_' in k) == want_d0]
    print(f"\n=== {label} ===")
    per = collections.defaultdict(float)
    tot = 0.0
    for k in sel:
        o, n = float(old[k].split('/')[0]), float(new[k].split('/')[0])
        per[k.split('_')[0]] += n - o
        tot += n - o
        if abs(n - o) > 1e-9:
            print(f"  {k:34s} {o:.4f} -> {n:.4f}   {n-o:+.4f}")
    for deck in sorted(per):
        print(f"  {'-- ' + deck:34s} {per[deck]:+.4f}")
    print(f"  {'== TOTAL (negative = NEW is better)':34s} {tot:+.4f}")
PY
echo ALLDONE
