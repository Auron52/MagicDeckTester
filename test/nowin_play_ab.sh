#!/usr/bin/env bash
# MTG_FS_NOWIN_CACHE on the PLAY path: does it save work, and does it change what the engine plays?
#
# Two questions, two instruments -- deliberately, because one cannot answer both:
#
#   WORK   deterministic -DMTG_PROFILE counters (search nodes, EnumeratePlans) from the SINGLE-RUN
#          path. Immune to machine load, unlike wall-clock -- which on these heavy-tailed decks has
#          produced a retracted claim in BOTH directions (see label-horizon-ladder.md).
#   PLAY   avg + play digest from the BATCH path, which is the only one that prints a digest.
#
# The distinction that decides the whole experiment: under a BOUNDED play budget a cache hit costs
# no budget, so the freed budget is spent going DEEPER rather than finishing sooner. Expect nodes to
# stay pinned near the cap and the effect to surface as a DIFFERENT DIGEST -- i.e. a play change
# needing a real A/B plus a ground-truth rebaseline, not a free speedup. A deck whose digest is
# unchanged is one where the budget was not binding, and there the memo is free.
#
# Do NOT read the avg column as a result: 20 games is far below the noise floor of the metric. It is
# here only as a coarse companion to the digest.
#
#   bash test/nowin_play_ab.sh <tag> [games] [seed]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

TAG=${1:?tag}; GAMES=${2:-20}; SEED=${3:-1001}
IBIN=${IBIN:-./build-instr/mtg}          # MTG_PROFILE build: counters
BBIN=${BBIN:-$(harness_bin)}             # normal Release: batch digest
THREADS=${THREADS:-4}
DECKS=${DECKS:-"burn Goblins slivers_vial Knights treasure_hunt Anti-Lifegain Dragonstorm Auras Hinata2"}

[ -x "$IBIN" ] || { echo "!! need the MTG_PROFILE build at $IBIN"; exit 1; }

OUT=logs/nowinab/$TAG; mkdir -p "$OUT"
nodes_of() { sed -nE 's/.*Search nodes \(steps\) *: ([0-9]+).*/\1/p' "$1"; }
enum_of()  { sed -nE 's/.*EnumeratePlans calls *: ([0-9]+).*/\1/p' "$1"; }

printf '%-16s %12s %12s %8s %12s %12s %8s   %s\n' \
       deck nodes_off nodes_on d_nodes enum_off enum_on d_enum 'play'
for d in $DECKS; do
    deck=$(ls "decks/$d/$d".cod "decks/$d/$d".txt 2>/dev/null | head -1)
    prof="decks/$d/$d.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d"; continue; }

    # --- WORK: single-run, instrumented. No --depth/--budget-ms == the deck's real play policy.
    for N in 0 1; do
        MTG_FS_NOWIN_CACHE=$N "$IBIN" "$deck" --profile "$prof" --games "$GAMES" --seed "$SEED" \
            --threads "$THREADS" > "$OUT/$d.work.N$N.log" 2>&1
    done
    n0=$(nodes_of "$OUT/$d.work.N0.log"); n1=$(nodes_of "$OUT/$d.work.N1.log")
    e0=$(enum_of  "$OUT/$d.work.N0.log"); e1=$(enum_of  "$OUT/$d.work.N1.log")

    # --- PLAY: batch, normal build. Same play policy (no depth/budget override in the job).
    man="$OUT/$d.manifest.json"
    { h_job "$d" "$deck" "$prof" "$GAMES" "$SEED"; } | h_manifest "$man" >/dev/null || continue
    for N in 0 1; do
        MTG_FS_NOWIN_CACHE=$N "$BBIN" --batch "$man" --threads "$THREADS" \
            > "$OUT/$d.play.N$N.log" 2>&1
    done
    a=$(grep -oE '^[A-Za-z0-9_.-]+: played=.*' "$OUT/$d.play.N0.log" | sort)
    b=$(grep -oE '^[A-Za-z0-9_.-]+: played=.*' "$OUT/$d.play.N1.log" | sort)
    if   [ -z "$a" ]     ; then play="*** NO OUTPUT ***"
    elif [ "$a" = "$b" ] ; then play="IDENTICAL"
    else play="DIFFERS  off=[$a]  on=[$b]"; fi

    dn=$(awk -v x="${n0:-0}" -v y="${n1:-0}" 'BEGIN{printf "%+.1f%%", (x>0)? 100*(y-x)/x : 0}')
    de=$(awk -v x="${e0:-0}" -v y="${e1:-0}" 'BEGIN{printf "%+.1f%%", (x>0)? 100*(y-x)/x : 0}')
    printf '%-16s %12s %12s %8s %12s %12s %8s   %s\n' \
           "$d" "${n0:--}" "${n1:--}" "$dn" "${e0:--}" "${e1:--}" "$de" "$play"
done
