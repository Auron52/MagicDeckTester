#!/usr/bin/env bash
# Does the no-win memo's benefit scale with SEARCH DEPTH?
#
# The prior: at d1 there are almost no interior nodes to memoize (depth 0 is the leaf), so the memo
# should be inert; as depth grows both the transposition rate and the number of provable dead ends
# grow, so the saving should widen. Worth measuring rather than assuming -- d3 cases DID move play in
# the regression suite (hinata_d3_s2002, dragonstorm_d3_s3003), so d3 is not inert.
#
# Reports DETERMINISTIC counters (search nodes, EnumeratePlans), not wall-clock, specifically so this
# can run alongside another job: virtual-ms budgets are node counts, so the numbers are exact under
# any machine load. Wall-clock would be contaminated -- and on these heavy-tailed decks wall-clock has
# already produced two retracted claims (see bound-qualified-nowin-memo.md).
#
# One invocation per (deck, depth, arm) rather than a pooled batch: PROF_REPORT prints on the
# single-run path only. Threads are deliberately low to leave the box to whatever else is running.
#
#   bash test/nowin_depth_curve.sh <tag> [games] [budget_ms] [depths...]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

TAG=${1:?tag}; GAMES=${2:-100}; BUDGET=${3:-20}; shift 3 2>/dev/null || shift $#
DEPTHS=${*:-"1 2 3 4 5"}
BIN=${BIN:-./build-instr/mtg}
THREADS=${THREADS:-4}
DECKS=${DECKS:-"Hinata2 treasure_hunt Auras Dragonstorm Goblins"}

[ -x "$BIN" ] || { echo "!! need the MTG_PROFILE build at $BIN"; exit 1; }
OUT=logs/nowindepth/$TAG; mkdir -p "$OUT"

printf '%-16s %5s %14s %14s %9s %9s\n' deck depth nodes_off nodes_on d_nodes d_enum
for d in $DECKS; do
    deck=$(ls "decks/$d/$d".cod "decks/$d/$d".txt 2>/dev/null | head -1)
    prof="decks/$d/$d.profile.json"
    [ -n "$deck" ] && [ -e "$prof" ] || { echo "!! skip $d"; continue; }
    for dep in $DEPTHS; do
        for N in 0 1; do
            MTG_FS_NOWIN_CACHE=$N "$BIN" "$deck" --profile "$prof" --games "$GAMES" --seed 900000 \
                --depth "$dep" --budget-ms "$BUDGET" --ignore-play-profile --threads "$THREADS" \
                > "$OUT/$d.d$dep.N$N.log" 2>&1
        done
        n0=$(sed -nE 's/.*Search nodes \(steps\) *: ([0-9]+).*/\1/p' "$OUT/$d.d$dep.N0.log")
        n1=$(sed -nE 's/.*Search nodes \(steps\) *: ([0-9]+).*/\1/p' "$OUT/$d.d$dep.N1.log")
        e0=$(sed -nE 's/.*EnumeratePlans calls *: ([0-9]+).*/\1/p' "$OUT/$d.d$dep.N0.log")
        e1=$(sed -nE 's/.*EnumeratePlans calls *: ([0-9]+).*/\1/p' "$OUT/$d.d$dep.N1.log")
        awk -v k="$d" -v p="$dep" -v a="${n0:-0}" -v b="${n1:-0}" -v c="${e0:-0}" -v e="${e1:-0}" \
            'BEGIN{printf "%-16s %5s %14s %14s %8.1f%% %8.1f%%\n", k, p, a, b,
                   (a>0?100*(b-a)/a:0), (c>0?100*(e-c)/c:0)}'
    done
done
