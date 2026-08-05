#!/usr/bin/env bash
# End-to-end LABEL GENERATION throughput: what does a row cost now vs before 2026-08-05?
#
# Three arms over identical games, each ONE pooled batch so the box stays saturated to one tail:
#   old   MTG_LABEL_LADDER=0 MTG_VALUE_LABEL_BNB=0 MTG_FS_NOWIN_CACHE=0   (pre-fix behaviour)
#   new   defaults                                                       (ladder + B&B)
#   cache defaults + MTG_FS_NOWIN_CACHE=1
#
# NOTE the arms are not producing the same thing: `old` emits the PESSIMISTIC label (~29% of rows
# carry a too-late win turn -- docs/design/label-horizon-ladder.md), so this is "cost of a correct
# row" vs "cost of a wrong one", which is the right framing for sizing a regeneration. `new` and
# `cache` must agree exactly, and that is asserted.
#
#   bash test/label_throughput.sh <tag> [games] [seed]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

TAG=${1:?tag}
GAMES=${2:-40}
SEED=${3:-888000}
K=${K:-3}
THREADS=${THREADS:-20}
BLOCK=${BLOCK:-4}
BIN=${BIN:-$(harness_bin)}
DECKS=${DECKS:-"burn:burn Goblins:Goblins Anti-Lifegain:Anti-Lifegain Dragonstorm:Dragonstorm slivers_vial:slivers_vial treasure_hunt:treasure_hunt Knights:Knights"}

OUT=logs/labelthroughput/$TAG
mkdir -p "$OUT"

printf '%-16s %6s %9s %9s %9s %9s %9s   %s\n' \
       deck rows sec_old sec_new sec_cache 'new/old' 'cache/old' 'new==cache'
for spec in $DECKS; do
    d=${spec%%:*}; s=${spec##*:}
    deck=$(ls "decks/$d/$s".cod "decks/$d/$s".txt 2>/dev/null | head -1)
    prof="decks/$d/$s.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d/$s"; continue; }

    man="$OUT/$s.manifest.json"
    {
        b=0
        while [ "$b" -lt "$GAMES" ]; do
            n=$(( GAMES - b < BLOCK ? GAMES - b : BLOCK ))
            h_job "${s}_$(printf '%04d' "$b")" "$deck" "$prof" "$n" "$(( SEED + b ))"
            b=$(( b + BLOCK ))
        done
    } | h_manifest "$man" >/dev/null || continue

    for arm in old new cache; do
        case $arm in
            old)   env_l=0; env_b=0; env_n=0 ;;
            new)   env_l=1; env_b=1; env_n=0 ;;
            cache) env_l=1; env_b=1; env_n=1 ;;
        esac
        rows="$OUT/$s.$arm.rows"; rm -f "$rows"
        st=$(date +%s.%N)
        MTG_DUMP_VALUE_ROWS="$rows" MTG_EVAL_ROWS_K="$K" \
        MTG_LABEL_LADDER="$env_l" MTG_VALUE_LABEL_BNB="$env_b" MTG_FS_NOWIN_CACHE="$env_n" \
            "$BIN" --batch "$man" --threads "$THREADS" > "$OUT/$s.$arm.log" 2>&1
        en=$(date +%s.%N)
        eval "sec_$arm=\$(awk -v a=\$st -v b=\$en 'BEGIN{printf \"%.1f\", b-a}')"
    done

    agree=$(cmp -s <(sort "$OUT/$s.new.rows" | grep -v '^#') \
                   <(sort "$OUT/$s.cache.rows" | grep -v '^#') && echo YES || echo "*** NO ***")
    rn=$(awk -v a="$sec_old" -v b="$sec_new"   'BEGIN{printf "%.2fx", (b>0)? a/b : 0}')
    rc=$(awk -v a="$sec_old" -v b="$sec_cache" 'BEGIN{printf "%.2fx", (b>0)? a/b : 0}')
    printf '%-16s %6s %9s %9s %9s %9s %9s   %s\n' "$s" \
        "$(grep -vc '^#' "$OUT/$s.new.rows")" "$sec_old" "$sec_new" "$sec_cache" "$rn" "$rc" "$agree"
done
