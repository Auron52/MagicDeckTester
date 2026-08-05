#!/usr/bin/env bash
# MTG_FS_NOWIN_CACHE pre-check: is the no-win memo worth an A/B on the PLAY path at all?
#
# The memo is proven and load-bearing OFFLINE (the labeller forces it; it is what makes Hinata
# labellable). Play is a different regime: budgets are ~20 virtual-ms, so `Exhausted()` fires
# routinely, and a truncation anywhere below a node suppresses the store at EVERY ancestor
# (g_fs_trunc_events). Before paying for a full A/B plus a ground-truth rebaseline, measure the
# two things that decide whether there is anything to A/B:
#
#   stored%   -- of the nodes that finished with a no-win, how many got to store? (low => the
#                truncation guard eats the feature and it is pure bookkeeping)
#   nowin hits -- how often does a stored no-win actually answer a later query? (low => the memo
#                is sound and cheap but simply never fires; nothing to gain)
#
# Deterministic counters, not wall-clock: virtual-ms budgets are node counts, so these numbers are
# reproducible under any machine load. Needs the MTG_PROFILE build (build-instr), and PROF_REPORT
# prints on the SINGLE-RUN path only -- hence one invocation per deck rather than one batch.
#
#   bash test/nowin_play_stats.sh <tag> [games] [seed]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

TAG=${1:?tag}; GAMES=${2:-20}; SEED=${3:-1001}
BIN=${BIN:-./build-instr/mtg}
THREADS=${THREADS:-4}
DECKS=${DECKS:-"burn Goblins slivers_vial Knights treasure_hunt Anti-Lifegain Dragonstorm Auras Hinata2"}

[ -x "$BIN" ] || { echo "!! need the MTG_PROFILE build: cmake -S . -B build-instr -DCMAKE_BUILD_TYPE=Release -DMTG_PROFILE=ON && cmake --build build-instr"; exit 1; }

OUT=logs/nowinplay/$TAG; mkdir -p "$OUT"
printf '%-16s %10s %10s %9s %9s %10s %9s %8s\n' \
       deck probes win_hits nowin_hit stale nowin_res stored% hit/1k
for d in $DECKS; do
    deck=$(ls "decks/$d/$d".cod "decks/$d/$d".txt 2>/dev/null | head -1)
    prof="decks/$d/$d.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d"; continue; }
    # No --depth / --budget-ms: use the deck's own value_play policy == the REAL play regime.
    MTG_FS_NOWIN_CACHE=1 "$BIN" "$deck" --profile "$prof" --games "$GAMES" --seed "$SEED" \
        --threads "$THREADS" > "$OUT/$d.log" 2>&1
    read -r probes winh nowinh stale <<<"$(sed -nE 's/.*FSLine memo probes *: ([0-9]+) *win hits: ([0-9]+) *nowin hits: ([0-9]+) *nowin stale: ([0-9]+).*/\1 \2 \3 \4/p' "$OUT/$d.log")"
    read -r res stored <<<"$(sed -nE 's/.*FSLine nowin results *: ([0-9]+) *stored: ([0-9]+).*/\1 \2/p' "$OUT/$d.log")"
    [ -n "${probes:-}" ] || { printf '%-16s %10s\n' "$d" "*** NO COUNTERS ***"; continue; }
    pct=$(awk -v s="${stored:-0}" -v r="${res:-0}" 'BEGIN{printf "%.1f", (r>0)? 100*s/r : 0}')
    per=$(awk -v h="${nowinh:-0}" -v p="${probes:-0}" 'BEGIN{printf "%.2f", (p>0)? 1000*h/p : 0}')
    printf '%-16s %10s %10s %9s %9s %10s %9s %8s\n' \
           "$d" "$probes" "$winh" "$nowinh" "$stale" "$res" "$pct" "$per"
done
