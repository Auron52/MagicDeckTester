#!/usr/bin/env bash
# MTG_FS_NOWIN_CACHE on the PLAY path: the METRIC A/B that decides adoption.
#
# What the cheaper checks already settled (see nowin_play_ab.sh / nowin_play_stats.sh):
#   * the memo is SOUND -- `stale` (a query rejected for too narrow a bound) is 0 on every deck;
#   * at each deck's real play policy it is a pure work saving with byte-identical play on 8 of 9
#     decks (Auras -14.1% nodes, treasure_hunt -6.6%), because the budget was not binding there;
#   * at d5/20ms it DOES change lines on treasure_hunt and Hinata -- smoke: 25/27 pass, the 2
#     failures being same-avg/different-digest. A cache hit costs no budget, so where the budget
#     binds the freed units are spent searching deeper, and the line moves.
#
# So the open question is only ever "is that line change neutral, better, or worse", and that is a
# metric question at a sample size smoke cannot reach. Held-out seeds by construction: base 900000,
# spaced 20000 apart per deck, so no two jobs can replay each other's games (a spacing smaller than
# games-per-job silently makes jobs share seeds and fakes a huge, zero-variance delta).
#
# One pooled manifest per arm, not one invocation per deck: the arms differ by an env var, which is
# per-process, so two batch runs is the minimum -- and each still pays a single load-imbalance tail.
#
#   bash test/nowin_metric_ab.sh <tag> [games] [depth] [budget_ms]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

TAG=${1:?tag}; GAMES=${2:-1000}; DEPTH=${3:-5}; BUDGET=${4:-20}
BIN=${BIN:-$(harness_bin)}
THREADS=${THREADS:-20}
DECKS=${DECKS:-"burn Goblins slivers_vial Knights treasure_hunt Anti-Lifegain Dragonstorm Auras Hinata2"}

OUT=logs/nowinmetric/$TAG; mkdir -p "$OUT"
man="$OUT/ab.manifest.json"
{
    i=0
    for d in $DECKS; do
        deck=$(ls "decks/$d/$d".cod "decks/$d/$d".txt 2>/dev/null | head -1)
        prof="decks/$d/$d.profile.json"
        [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d" >&2; continue; }
        h_job "$d" "$deck" "$prof" "$GAMES" $((900000 + i * 20000)) \
              depth="$DEPTH" budget_ms="$BUDGET" ignore_play_profile=true
        i=$((i + 1))
    done
} | h_manifest "$man" >/dev/null

for N in 0 1; do
    echo ">>> arm MTG_FS_NOWIN_CACHE=$N  ($(date '+%H:%M:%S'))" >&2
    MTG_FS_NOWIN_CACHE=$N "$BIN" --batch "$man" --threads "$THREADS" > "$OUT/arm.N$N.log" 2>&1
done

echo ""
echo "=== d$DEPTH budget=${BUDGET}ms, $GAMES games/deck, held-out seeds from 900000 ==="
h_delta "$OUT/arm.N0.log" "$OUT/arm.N1.log" "nowin=0" "nowin=1"
echo ""
echo "(negative = the memo is better on the loss-penalized metric; a deck whose digest is"
echo " unchanged did not move at all -- check arm.N*.log for the digests.)"
