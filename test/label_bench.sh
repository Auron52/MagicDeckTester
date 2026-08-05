#!/usr/bin/env bash
# Value-LABEL generation micro-benchmark (offline path only -- no play change, no GT).
#
# Runs a FIXED, small set of games with MTG_DUMP_VALUE_ROWS on and reports, per deck,
# (wall seconds, rows produced, a hash of the sorted rows). Single-threaded so the wall
# time is a clean cost signal and the rows are deterministic; the row hash is the
# CORRECTNESS gate for any change to the label search (labels must not move unless the
# change is meant to move them, in which case the diff is inspected).
#
#   bash test/label_bench.sh <tag> [deck:games ...]
#   env: K=3  THREADS=1  BIN=build/Release/mtg  MTG_VALUE_LABEL_BUDGET_MS (passed through)
#
# Output goes to logs/labelbench/<tag>/.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

TAG=${1:?tag}; shift
K=${K:-3}
THREADS=${THREADS:-1}
BIN=${BIN:-build/Release/mtg}
BASE=${BASE:-777000}

# deck-dir:stem:games -- defaults span cheap -> pathological
SPECS=("$@")
if [ ${#SPECS[@]} -eq 0 ]; then
    SPECS=(Dragonstorm:Dragonstorm:4 Anti-Lifegain:Anti-Lifegain:4 burn:burn:6)
fi

OUT=logs/labelbench/$TAG
mkdir -p "$OUT"
printf '%-16s %8s %8s %10s  %s\n' deck games rows sec sha1

for spec in "${SPECS[@]}"; do
    dir=${spec%%:*}; rest=${spec#*:}
    stem=${rest%%:*}; games=${rest#*:}
    deck=$(ls "decks/$dir/$stem".cod "decks/$dir/$stem".txt 2>/dev/null | head -1)
    prof="decks/$dir/$stem.profile.json"
    [ -n "$deck" ] || { echo "!! no deck file for $dir/$stem"; continue; }

    rows=$OUT/$stem.rows
    rm -f "$rows"
    t0=$(date +%s.%N)
    MTG_DUMP_VALUE_ROWS="$rows" MTG_EVAL_ROWS_K="$K" \
        "$BIN" "$deck" --profile "$prof" --games "$games" --seed "$BASE" \
               --threads "$THREADS" > "$OUT/$stem.log" 2>&1
    rc=$?
    t1=$(date +%s.%N)
    sec=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
    n=$(grep -vc '^#' "$rows" 2>/dev/null || echo 0)
    # hash label+features only (drop nothing -- rows already carry seed/turn), sorted for order-independence
    h=$(grep -v '^#' "$rows" 2>/dev/null | sort | sha1sum | cut -c1-16)
    printf '%-16s %8s %8s %10s  %s%s\n' "$stem" "$games" "$n" "$sec" "$h" \
        "$( [ $rc -eq 0 ] || echo "  (rc=$rc)")"
done
