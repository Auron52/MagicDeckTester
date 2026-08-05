#!/usr/bin/env bash
# MTG_LADDER_VALUE_LEAF: does laddering the warm-up passes on the cheap value leaf change ANY result?
#
# The claim is byte-identity of the COMMITTED line at an UNBOUNDED budget, argued from three
# structural facts (see FullSearchLine): a warm-up pass cannot leave an FSLineCache entry the
# committed pass can read (turn+depth is a per-pass constant folded into the key), a value-leaf pass
# never writes the leaf table (it returns before SimulateToEnd), and with no budget there is no
# budget coupling. This checks the claim rather than trusting it -- per-game win turns AND the play
# digest must match exactly.
#
# H-CELL SHAPE. The matrix's H cells run MTG_VALUE_MODEL=0 with no model attached at all, so the
# warm-up passes would have nothing cheap to fall back to. The arm therefore ATTACHES the model
# (MTG_VALUE_PROFILE) while leaving MTG_VALUE_MODEL=0, so the committed pass is still pure heuristic.
#
#   bash test/ladder_value_leaf_check.sh <tag> [games] [seed] [depths...]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

TAG=${1:?tag}; GAMES=${2:-40}; SEED=${3:-424000}; shift 3 2>/dev/null || shift $#
DEPTHS=${*:-"1 2 3 4"}
BIN=${BIN:-$(harness_bin)}
THREADS=${THREADS:-20}
DECKS=${DECKS:-"Goblins:Goblins Anti-Lifegain:Anti-Lifegain Dragonstorm:Dragonstorm treasure_hunt:treasure_hunt Knights:Knights burn:burn slivers_vial:slivers_vial"}

OUT=logs/laddervl/$TAG; mkdir -p "$OUT"
printf '%-16s %6s %10s %10s %9s   %s\n' deck depth sec_off sec_on speedup 'result'
for spec in $DECKS; do
    d=${spec%%:*}; s=${spec##*:}
    deck=$(ls "decks/$d/$s".cod "decks/$d/$s".txt 2>/dev/null | head -1)
    prof="decks/$d/$s.profile.json"
    vmodel="decks/$d/$s.value.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d/$s"; continue; }
    [ -e "$vmodel" ] || { printf '%-16s %6s %10s %10s %9s   %s\n' "$s" - - - - "SKIP (no value model to ladder on)"; continue; }

    for dep in $DEPTHS; do
        man="$OUT/$s.d$dep.manifest.json"
        # budget 0 == UNBOUNDED, the regime the identity claim is about and the one the matrix uses.
        # ignore_play_profile: the deck profile LOCKS target_depth (value_play), so a bare depth= is
        # rejected. This is the same override the matrix generator passes (--ignore-play-profile).
        { h_job "${s}_d${dep}" "$deck" "$prof" "$GAMES" "$SEED" depth="$dep" budget_ms=0 \
                 ignore_play_profile=true; } \
            | h_manifest "$man" >/dev/null || continue
        for L in 0 1; do
            st=$(date +%s.%N)
            MTG_VALUE_MODEL=0 MTG_VALUE_PROFILE="$vmodel" MTG_LADDER_VALUE_LEAF="$L" \
                "$BIN" --batch "$man" --threads "$THREADS" > "$OUT/$s.d$dep.L$L.log" 2>&1
            en=$(date +%s.%N)
            eval "sec$L=\$(awk -v a=\$st -v b=\$en 'BEGIN{printf \"%.1f\", b-a}')"
        done
        # The fingerprint is "<name>: played=.. avg=.. digest=..": avg is the metric, digest is the play.
        a=$(grep -oE '^[A-Za-z0-9_.-]+: played=.*' "$OUT/$s.d$dep.L0.log" | sort)
        b=$(grep -oE '^[A-Za-z0-9_.-]+: played=.*' "$OUT/$s.d$dep.L1.log" | sort)
        if [ -z "$a" ]; then res="*** NO OUTPUT ***"
        elif [ "$a" = "$b" ]; then res="IDENTICAL (avg+digest)"
        else res="*** DIFFERS ***  off=[$a]  on=[$b]"; fi
        sp=$(awk -v x="$sec0" -v y="$sec1" 'BEGIN{printf "%.2fx", (y>0)? x/y : 0}')
        printf '%-16s %6s %10s %10s %9s   %s\n' "$s" "$dep" "$sec0" "$sec1" "$sp" "$res"
    done
done
