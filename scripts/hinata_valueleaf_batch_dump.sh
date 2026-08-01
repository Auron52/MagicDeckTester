#!/usr/bin/env bash
# Hinata value-leaf ROW DUMP as ONE POOLED BATCH -- the repo's batching rule, which the chunked
# driver in hinata_valueleaf_pipeline.sh violates.
#
# WHAT WAS WRONG. run_chunked_dump is a `while` loop of sequential `mtg` invocations, one per
# 100-game chunk, each with its own --threads 24. Every invocation pays its OWN load-imbalance
# tail, and a chunk only commits on clean exit -- so a couple of pathological games hold the whole
# chunk hostage. Observed 2026-08-01: chunk 215 finished 98 of 100 games in ~5h, then TWO games
# (seeds 20249, 20280) ran ~3h more with ~20 of 24 cores idle and not one row committed.
#
# WHY POOLING FIXES IT. One `mtg --batch` over one manifest puts every game of every job in a
# single work queue, so the runner keeps cores saturated to ONE tail for the whole run. The two
# slow games then occupy two threads while the other 22 keep producing rows, instead of being a
# barrier. Same lesson as the regression harness's per-mode pooling: one tail, not one per item.
#
# DURABILITY IS NOT WHY CHUNKS EXISTED, though it looked that way. The row writer (AIEngine.cpp,
# s_value_rows_path) is mutex-guarded, opened in append mode, and FLUSHES EVERY ROW -- so a row is
# on disk the moment it is produced and an interruption loses nothing already written. The chunk
# machinery only ever tracked a RESUME POINT, and it paid for that with the tail above.
#
# RESUMING IS THEREFORE TRIVIAL AND SAFE. Every row carries `seed` and `turn`, which identify a
# position exactly, so a re-run's overlap is removed by deduplicating on those two columns rather
# than by bookkeeping that can go wrong. Just re-run this script; then `collect`.
#
#   bash scripts/hinata_valueleaf_batch_dump.sh [games] [rows_file]
#   bash scripts/hinata_valueleaf_batch_dump.sh collect     # dedupe + report
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

ROWS=logs/eval/hinata_value_pooled.rows
DECK=$(h_deck Hinata2) || exit 1
PROF=$(h_profile Hinata2) || exit 1

# ---- collect: fold every row source into one deduplicated training file --------------------
if [ "${1:-}" = "collect" ]; then
    OUT=logs/eval/hinata_rows_B.all.rows
    # Sources: the pooled dump, the rescued rows from the chunk the tail stranded, every complete
    # legacy chunk, and the pre-chunking legacy file.
    SRC=$( ls logs/eval/hinata_value_pooled.rows \
              logs/eval/rows_B_rescue_chunk215_564rows.rows \
              logs/eval/rows_B/chunk_*.rows \
              logs/eval/hinata_value_v2b.rows 2>/dev/null )
    echo "sources:"; for f in $SRC; do echo "   $(grep -vc '^#' "$f") rows  $f"; done
    # Keep ONE header, then dedupe on (seed, turn) -- the LAST TWO fields. Keyed off NF rather than
    # hard-coded column numbers so adding a feature to the row cannot silently break the dedupe.
    # Sorted afterwards because the dump is multi-threaded: row order varies run to run, and an
    # unsorted training file makes training irreproducible.
    { grep -h '^#' $SRC | head -1
      cat $SRC | grep -v '^#' | awk '!seen[$(NF-1)" "$NF]++' | sort
    } > "$OUT"
    echo "-> $OUT: $(grep -vc '^#' "$OUT") unique rows"
    exit 0
fi

GAMES=${1:-2000}
ROWS=${2:-$ROWS}

# Per-game seed is base_seed + gi with gi restarting at 0 each invocation, so distinct jobs need
# disjoint seed ranges or they replay the same games. One job per 250-game block, all pooled into
# ONE queue by the single --batch call below; the blocks exist only to keep seed ranges disjoint,
# NOT to serialise the work.
BLOCK=250
BASE=30030      # fresh: clear of 20020+gi used by the chunked runs and of the suite's seed sets
{
    b=0
    while [ "$b" -lt "$GAMES" ]; do
        n=$(( GAMES - b < BLOCK ? GAMES - b : BLOCK ))
        h_job "hinata_rows_$(printf '%05d' "$b")" "$DECK" "$PROF" "$n" "$(( BASE + b ))"
        b=$(( b + BLOCK ))
    done
} | h_manifest "$ROWS.manifest.json" >/dev/null || exit 1

echo "pooled dump: $GAMES games in $(( (GAMES + BLOCK - 1) / BLOCK )) jobs, ONE queue -> $ROWS"
echo "rows before: $(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)"
MTG_DUMP_VALUE_ROWS="$ROWS" MTG_EVAL_ROWS_K=3 \
    "$(harness_bin)" --batch "$ROWS.manifest.json" --threads 24 \
    > "$ROWS.batch.log" 2>&1
echo "rows after : $(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)"
echo "next: bash scripts/hinata_valueleaf_batch_dump.sh collect"
