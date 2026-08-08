#!/usr/bin/env bash
# Top up tractability-capped matrix cells, then re-derive the metadata.
#
# WHY THIS IS A SCRIPT. The H cells ARE the crossover -- they decide which evaluator escalation uses
# at each rung it climbs to -- so a condemned H cell leaves a HOLE in the answer rather than saving
# cost. Recovering one takes three steps that are each easy to get wrong alone:
#   1. clear the `intractable` flag IN THE STATE FILE. Raising --never-condemn-at-or-below on a
#      resume does nothing by itself: a capped cell sits at games == reference_target, so needs() is
#      false, so it is never rescheduled, so the condemnation check never re-runs.
#   2. re-run the matrix incrementally with the guard raised, so it cannot re-condemn them.
#   3. delete the D marker so the metadata is re-derived from the FULL table, not the starved one.
#
#   bash scripts/valueleaf_topup.sh decks/<Deck> [max-depth]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

DECK_DIR=${1:?deck dir}
MAXD=${2:-5}
STEM=$(basename "$DECK_DIR")
KEY=$(echo "$STEM" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9\n' '_')
VLQ=${VLQ:-logs/vlq_$KEY}
CELLS=$VLQ/matrix.txt.cells.json

[ -s "$CELLS" ] || { echo "no matrix state at $CELLS -- run the queue first"; exit 1; }
if pgrep -f "valueleaf_depth_matrix" >/dev/null 2>&1; then
    echo "a matrix run is still going -- let it finish, or it will fight this one for the box"; exit 1
fi

echo "== before =="
bash scripts/valueleaf_uncondemn.sh "$CELLS" "$MAXD" || exit 1
rm -f "$VLQ/done/C_matrix" "$VLQ/done/D_meta"
echo "cleared C_matrix + D_meta markers -- the queue will top up, then re-derive."
echo
echo "now run:  bash scripts/valueleaf_regen_queue.sh run $DECK_DIR"
