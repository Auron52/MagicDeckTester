#!/usr/bin/env bash
# MTG_FS_NOWIN_CACHE correctness check on the OFFLINE label path.
#
# Caching a bound-qualified no-win must not change any answer -- it only saves the search from
# re-proving a refutation it already proved. On the label path the budget is effectively unbounded,
# so there is nothing for the freed budget to change: every label must come out IDENTICAL. (In real
# play a cache hit consumes no budget, so the freed budget DOES deepen the search -- that arm is a
# separate A/B, not this one.)
#
#   bash test/nowin_cache_check.sh <tag> [games] [seed]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

TAG=${1:?tag}
GAMES=${2:-6}
SEED=${3:-666000}
K=${K:-3}
BIN=${BIN:-build/RelWithDebInfo/mtg}
DECKS=${DECKS:-"Goblins:Goblins Anti-Lifegain:Anti-Lifegain Dragonstorm:Dragonstorm slivers_vial:slivers_vial treasure_hunt:treasure_hunt Knights:Knights"}

OUT=logs/nowincache/$TAG
mkdir -p "$OUT"
printf '%-16s %7s %9s %9s %8s   %s\n' deck rows sec_off sec_on speedup labels
for spec in $DECKS; do
    d=${spec%%:*}; s=${spec##*:}
    deck=$(ls "decks/$d/$s".cod "decks/$d/$s".txt 2>/dev/null | head -1)
    prof="decks/$d/$s.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d/$s"; continue; }
    for N in 0 1; do
        rows="$OUT/$s.N$N.rows"; rm -f "$rows"
        st=$(date +%s.%N)
        MTG_DUMP_VALUE_ROWS="$rows" MTG_EVAL_ROWS_K="$K" MTG_LABEL_LADDER=1 MTG_FS_NOWIN_CACHE="$N" \
            "$BIN" "$deck" --profile "$prof" --games "$GAMES" --seed "$SEED" --threads 1 \
            > "$OUT/$s.N$N.log" 2>&1
        en=$(date +%s.%N)
        eval "sec$N=\$(awk -v a=\$st -v b=\$en 'BEGIN{printf \"%.2f\", b-a}')"
    done
    same=$(cmp -s <(grep -v '^#' "$OUT/$s.N0.rows") <(grep -v '^#' "$OUT/$s.N1.rows") \
             && echo IDENTICAL || echo "*** DIFFER ***")
    sp=$(awk -v a="${sec0}" -v b="${sec1}" 'BEGIN{printf "%.2fx", (b>0)? a/b : 0}')
    printf '%-16s %7s %9s %9s %8s   %s\n' "$s" \
        "$(grep -vc '^#' "$OUT/$s.N0.rows")" "$sec0" "$sec1" "$sp" "$same"
done
