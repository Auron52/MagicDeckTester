#!/usr/bin/env bash
# Generic pooled value-row dump -- the deck-agnostic form of scripts/hinata_valueleaf_batch_dump.sh.
#
# ONE POOLED BATCH, always. Every game of every block goes into a single work queue, so the box stays
# saturated to ONE load-imbalance tail. The pattern this replaces -- a loop of per-chunk `mtg`
# invocations, each with its own --threads 24 -- pays a tail PER INVOCATION: measured 2026-08-01, one
# chunk finished 98 of 100 games in ~5 h then spent ~3 h on two games with ~20 of 24 cores idle,
# committing nothing. Same lesson as the regression harness's per-mode pooling: one tail, not one each.
#
# SEED DISCIPLINE. Per-game seed is base+game_index with game_index restarting at 0 every invocation,
# so two invocations sharing a base REPLAY the same games and every row of the second is dropped by
# the (seed,turn) dedupe -- hours of work for zero new rows. Blocks below are spaced exactly BLOCK, and
# a repeat run must advance BASE past the previous run's total games.
#
# DURABILITY is free: the row writer is mutex-guarded, append-mode, and flushes every row, so rows are
# on disk as produced and an interruption loses only in-flight games. Resume by re-running and
# deduplicating on (seed,turn) -- every row carries both.
#
#   bash scripts/valueleaf_row_dump.sh <deck-dir> <stem> <games> [rows-file] [base]
#   env: K=3 (searched-label breadth) ROLLOUT=0 (1 = cheap non-clairvoyant d0 labels)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

DECK_DIR=${1:?deck dir}          # e.g. decks/Knights
STEM=${2:?deck stem}             # e.g. Knights
GAMES=${3:?games}
ROWS=${4:-logs/eval/${STEM}_value.rows}
BASE=${5:-30030}
K=${K:-3}
ROLLOUT=${ROLLOUT:-0}
BLOCK=${BLOCK:-250}

DECK=$(ls "$DECK_DIR/$STEM".cod "$DECK_DIR/$STEM".txt 2>/dev/null | head -1)
PROF="$DECK_DIR/$STEM.profile.json"
[ -n "$DECK" ] || { echo "no deck file in $DECK_DIR for stem $STEM"; exit 1; }
[ -e "$PROF" ] || { echo "no profile at $PROF"; exit 1; }
mkdir -p "$(dirname "$ROWS")"

{
    b=0
    while [ "$b" -lt "$GAMES" ]; do
        n=$(( GAMES - b < BLOCK ? GAMES - b : BLOCK ))
        h_job "${STEM}_rows_$(printf '%05d' "$b")" "$DECK" "$PROF" "$n" "$(( BASE + b ))"
        b=$(( b + BLOCK ))
    done
} | h_manifest "$ROWS.manifest.json" >/dev/null || exit 1

echo "pooled dump: $STEM $GAMES games in $(( (GAMES + BLOCK - 1) / BLOCK )) jobs, ONE queue (K=$K rollout=$ROLLOUT) -> $ROWS"
echo "rows before: $(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)"
MTG_DUMP_VALUE_ROWS="$ROWS" MTG_EVAL_ROWS_K="$K" MTG_EVAL_ROWS_ROLLOUT="$ROLLOUT" \
    "$(harness_bin)" --batch "$ROWS.manifest.json" --threads 24 > "$ROWS.batch.log" 2>&1
echo "rows after : $(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)"
