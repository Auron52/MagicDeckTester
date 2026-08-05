#!/usr/bin/env bash
# MTG_VALUE_LABEL_BNB under the horizon ladder: lossless, and how much does it save?
#
# B&B was rejected because it produced EARLIER labels than the unpruned arm -- which was really the
# unpruned arm being wrong (see docs/design/label-horizon-ladder.md). With the ladder the premise
# holds in both arms, so cross-candidate branch-and-bound must now be exactly lossless for the value
# label (report.earliest, the min over candidates) while stopping the ladder at the first winning
# pass. This checks both halves: labels IDENTICAL, and the wall-clock saving.
#
#   bash test/label_bnb_check.sh <tag> [games] [seed]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

TAG=${1:?tag}
GAMES=${2:-6}
SEED=${3:-666000}
K=${K:-3}
BIN=${BIN:-build/RelWithDebInfo/mtg}
NOWIN=${NOWIN:-1}
DECKS=${DECKS:-"Goblins:Goblins Anti-Lifegain:Anti-Lifegain Dragonstorm:Dragonstorm slivers_vial:slivers_vial treasure_hunt:treasure_hunt Knights:Knights burn:burn"}

OUT=logs/labelbnb/$TAG
mkdir -p "$OUT"
printf '%-16s %7s %9s %9s %8s   %s\n' deck rows sec_off sec_on speedup labels
for spec in $DECKS; do
    d=${spec%%:*}; s=${spec##*:}
    deck=$(ls "decks/$d/$s".cod "decks/$d/$s".txt 2>/dev/null | head -1)
    prof="decks/$d/$s.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d/$s"; continue; }
    for B in 0 1; do
        rows="$OUT/$s.B$B.rows"; rm -f "$rows"
        st=$(date +%s.%N)
        MTG_DUMP_VALUE_ROWS="$rows" MTG_EVAL_ROWS_K="$K" MTG_LABEL_LADDER=1 \
        MTG_FS_NOWIN_CACHE="$NOWIN" MTG_VALUE_LABEL_BNB="$B" \
            "$BIN" "$deck" --profile "$prof" --games "$GAMES" --seed "$SEED" --threads 1 \
            > "$OUT/$s.B$B.log" 2>&1
        en=$(date +%s.%N)
        eval "sec$B=\$(awk -v a=\$st -v b=\$en 'BEGIN{printf \"%.2f\", b-a}')"
    done
    same=$(cmp -s <(grep -v '^#' "$OUT/$s.B0.rows") <(grep -v '^#' "$OUT/$s.B1.rows") \
             && echo IDENTICAL || echo "*** DIFFER ***")
    sp=$(awk -v a="${sec0}" -v b="${sec1}" 'BEGIN{printf "%.2fx", (b>0)? a/b : 0}')
    printf '%-16s %7s %9s %9s %8s   %s\n' "$s" \
        "$(grep -vc '^#' "$OUT/$s.B0.rows")" "$sec0" "$sec1" "$sp" "$same"
done
