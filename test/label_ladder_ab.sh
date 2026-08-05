#!/usr/bin/env bash
# MTG_LABEL_LADDER A/B -- correctness first, cost second.
#
# The claim under test: the labeller's single-depth FSLineTail call breaks FSLineWin's first-win
# shortcut premise, so labels are PESSIMISTIC. A sound fix can therefore only ever move a label
# EARLIER. One label that moves LATER refutes the fix, so that is the gate this script reports.
#
#   bash test/label_ladder_ab.sh <tag> [games] [seed]
#
# Per deck, runs both arms over the same games (pooled into one batch per arm so the box stays
# saturated) and reports rows, wall seconds, and the earlier/later split.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

TAG=${1:?tag}
GAMES=${2:-8}
SEED=${3:-555000}
K=${K:-3}
THREADS=${THREADS:-8}
BLOCK=${BLOCK:-2}
BIN=${BIN:-build/Profile/mtg}

OUT=logs/labelladder/$TAG
mkdir -p "$OUT"

DECKS=${DECKS:-"burn:burn Goblins:Goblins Anti-Lifegain:Anti-Lifegain Dragonstorm:Dragonstorm slivers_vial:slivers_vial treasure_hunt:treasure_hunt Knights:Knights Hinata2:Hinata2"}

printf '%-16s %7s %7s %9s %9s   %s\n' deck rows_off rows_on sec_off sec_on 'label movement'
for spec in $DECKS; do
    d=${spec%%:*}; s=${spec##*:}
    deck=$(ls "decks/$d/$s".cod "decks/$d/$s".txt 2>/dev/null | head -1)
    prof="decks/$d/$s.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d/$s (no deck or profile)"; continue; }

    man="$OUT/$s.manifest.json"
    {
        b=0
        while [ "$b" -lt "$GAMES" ]; do
            n=$(( GAMES - b < BLOCK ? GAMES - b : BLOCK ))
            h_job "${s}_$(printf '%04d' "$b")" "$deck" "$prof" "$n" "$(( SEED + b ))"
            b=$(( b + BLOCK ))
        done
    } | h_manifest "$man" >/dev/null || continue

    for L in 0 1; do
        rows="$OUT/$s.L$L.rows"; rm -f "$rows"
        st=$(date +%s.%N)
        MTG_DUMP_VALUE_ROWS="$rows" MTG_EVAL_ROWS_K="$K" MTG_LABEL_LADDER="$L" \
            "$BIN" --batch "$man" --threads "$THREADS" > "$OUT/$s.L$L.log" 2>&1
        en=$(date +%s.%N)
        eval "sec$L=\$(awk -v a=\$st -v b=\$en 'BEGIN{printf \"%.1f\", b-a}')"
        eval "n$L=\$(grep -vc '^#' \"\$rows\" 2>/dev/null || echo 0)"
    done

    # Join on (seed,turn) -- the last two fields -- so a thread-order difference cannot fake a move.
    mv=$(join -j1 \
            <(grep -v '^#' "$OUT/$s.L0.rows" 2>/dev/null | awk '{print $(NF-1)"_"$NF, $1}' | sort) \
            <(grep -v '^#' "$OUT/$s.L1.rows" 2>/dev/null | awk '{print $(NF-1)"_"$NF, $1}' | sort) \
         | awk '{if ($3+0 < $2+0) e++; else if ($3+0 > $2+0) l++; else same++;
                 d += ($3+0)-($2+0)}
                END{printf "%d earlier, %d LATER, %d same (mean %+.4f)", e+0, l+0, same+0,
                            (e+l+same)? d/(e+l+same) : 0}')
    printf '%-16s %7s %7s %9s %9s   %s\n' "$s" "${n0:-0}" "${n1:-0}" "${sec0:-?}" "${sec1:-?}" "$mv"
done
