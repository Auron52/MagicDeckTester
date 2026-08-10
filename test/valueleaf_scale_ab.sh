#!/usr/bin/env bash
# Scale-verify a STAGED value-leaf sidecar against the LIVE one on FRESH seeds.
#
#   bash test/valueleaf_scale_ab.sh decks/<Deck> [logs/eval/<Stem>.value.STAGED.json]
#
# WHY THIS EXISTS. `valueleaf.sh` phase E judges a regenerated model on 8 seeds x 1000 games. That is
# enough to spot a large move and NOT enough to adopt on: Knights read "inert" at 8 seeds and then
# showed +0.00020 (t=+2.74, 0 of 16 seeds better) on 16 FRESH seeds x 2500 games. A play-neutral
# phase-E result is therefore an invitation to measure properly, not a verdict. This runs the same
# two arms over ~100x the games on seeds nothing else in the repo has touched.
#
# THE TWO INVARIANTS IT ENFORCES, both of which have produced a fake result before:
#
#   * SEEDS ARE FRESH AND TILE EXACTLY. Per-game identity is base_seed + game_index, so bases spaced
#     closer than games-per-job make jobs REPLAY games -- which reports enormous significance off a
#     handful of distinct games (the tell is near-zero variance across seeds; it once faked -14.4 s).
#     SPACING == GAMES here, so the bases tile the id space with no gap and no overlap, and
#     vlq_ab_report.py re-asserts `distinct ids == games/arm` on every run.
#   * ONE POOLED BATCH. Every job of BOTH arms goes into a single `mtg --batch` so the run pays one
#     load-imbalance tail instead of one per arm, and neither arm gets a quieter machine than the
#     other -- an arm measured against a busier box is an arm with a handicap.
#
# The arms are scratch deck folders whose siblings are symlinked, differing ONLY in value.json --
# because sidecar PRESENCE alone activates the depth-aware hybrid, so "the same deck with a flag off"
# is not an available baseline. Nothing here writes a live sidecar; adoption stays a separate call.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

DECK_DIR=${1:?usage: valueleaf_scale_ab.sh decks/<Deck> [staged.value.json]}
STEM=$(basename "$DECK_DIR")
KEY=$(echo "$STEM" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9\n' '_')
STAGED=${2:-logs/eval/$STEM.value.STAGED.json}

# Fresh id space. Everything else in the repo lives below ~1M (rows 900k, phase-E A/B 600k, sweeps
# 610k, matrix in the thousands, regression suite low), so 3M+ collides with nothing -- which is the
# whole point of "fresh": a seed reused from training or from phase E measures memorised games.
SEED0=${SEED0:-3000000}
SEEDS=${SEEDS:-128}
GAMES=${GAMES:-25000}         # per seed per arm; SPACING == GAMES (see invariant above)
OUT=${OUT:-logs/vlq_goblins_scale}

[ -d "$DECK_DIR" ] || { echo "no such deck dir: $DECK_DIR"; exit 2; }
[ -s "$STAGED" ]   || { echo "no staged sidecar at $STAGED -- run scripts/valueleaf.sh first"; exit 2; }
[ -x build/Release/mtg ] || { echo "no build/Release/mtg -- run ./build.sh"; exit 2; }
deck_file() { ls "$1/$2".cod "$1/$2".txt 2>/dev/null | head -1; }

mkdir -p "$OUT"
log() { echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$OUT/scale.log"; }

# Same construction as valueleaf.sh's make_variant_deck: symlink every sibling so the ~600 MB
# keep-model caches are not copied, then place the arm's value.json. An absent/empty source means NO
# sidecar, which is the correct live arm for a deck that never had one.
make_variant_deck() {   # dest src-dir stem value.json
    local dest=$1 src=$2 stem=$3 val=$4 f b
    rm -rf "$dest"; mkdir -p "$dest"
    for f in "$src"/*; do
        b=$(basename "$f"); [ "$b" = "$stem.value.json" ] && continue
        ln -sf "$(realpath "$f")" "$dest/$b"
    done
    [ -s "$val" ] && cp "$val" "$dest/$stem.value.json"
    return 0
}

VROOT=$OUT/variants
make_variant_deck "$VROOT/live"   "$DECK_DIR" "$STEM" "$DECK_DIR/$STEM.value.json"
make_variant_deck "$VROOT/staged" "$DECK_DIR" "$STEM" "$STAGED"

# Fail loudly if the arms are the same file. A scale run that measures a model against itself returns
# a clean, confident, meaningless zero -- and reads exactly like a genuine "inert" result.
if cmp -s "$VROOT/live/$STEM.value.json" "$VROOT/staged/$STEM.value.json"; then
    echo "ABORT: live and staged sidecars are IDENTICAL -- there is nothing to measure."; exit 2
fi

log "=== $KEY value-leaf SCALE A/B: live vs staged ==="
log "  staged   : $STAGED"
log "  seeds    : $SEEDS bases from $SEED0, spacing $GAMES (tiles exactly: ids $SEED0..$(( SEED0 + SEEDS*GAMES - 1 )))"
log "  games    : $(( SEEDS * GAMES )) per arm, $(( SEEDS * GAMES * 2 )) total, ONE pooled batch"

{ for i in $(seq 0 $(( SEEDS - 1 ))); do
    s=$(( SEED0 + i * GAMES ))
    h_job "${KEY}-live_s$s"   "$(deck_file "$VROOT/live" "$STEM")"   "$VROOT/live/$STEM.profile.json"   "$GAMES" "$s"
    h_job "${KEY}-staged_s$s" "$(deck_file "$VROOT/staged" "$STEM")" "$VROOT/staged/$STEM.profile.json" "$GAMES" "$s"
  done; } | h_manifest "$OUT/scale.manifest.json" >/dev/null
log "  jobs     : $(grep -c '"name"' "$OUT/scale.manifest.json") in one queue"

./build/Release/mtg --batch "$OUT/scale.manifest.json" --threads 24 \
    > "$OUT/scale.batch.log" 2> "$OUT/scale.batch.err"
log "batch done"

grep "^$KEY-" "$OUT/scale.batch.log" | sed "s/^$KEY-//" > "$OUT/scale.ab.log"
log "=== $KEY: regenerated value-leaf vs live, $SEEDS fresh seeds x $GAMES games ==="
python3 scripts/vlq_ab_report.py "$OUT/scale.ab.log" live 2>&1 | tee -a "$OUT/scale.log"
log "=== COMPLETE -- nothing adopted ==="
