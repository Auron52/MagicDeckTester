#!/usr/bin/env bash
# mine_heuristics.sh -- one-command heuristic candidate generator for a deck.
#
# Drives the enumerate-all-earliest-wins dump (MTG_DUMP_EWINS) over a chunk of games and
# every pre-combat decision turn, then runs analyze_earliest_wins.py to mine ORDER /
# INCLUSION / LAND-FETCH rule candidates. The output is a REPORT of grounded candidate
# heuristics for the DecisionProvider -- you still encode + A/B-validate them (analyze-deck
# Stage 5e step 6) before shipping; this just finds them fast.
#
# EXPENSIVE: a full-game rollout per candidate per decision. Bound it with GAMES / BUDGET;
# overnight-safe. Cast ORDERINGS are expanded (MTG_SEARCH_ORDER=1) so ORDER rules appear.
#
# Usage:
#   scripts/mine_heuristics.sh <deckfile> [profile] [GAMES] [SEED] [DEPTH] [BUDGET_MS] [TURN]
#     profile  default <deckfile-without-ext>.profile.json
#     GAMES    default 500 (games PER seed)
#     SEEDS    default "2002 3003" (space-separated; >=2 seeds guards against seed-overfit).
#              The positional 4th arg or $SEED sets a SINGLE seed (overrides SEEDS).
#     DEPTH    default 5
#     BUDGET   default 3000 (virtual-ms per decision)
#     TURN     default 0 = mine EVERY pre-combat decision; or a single turn number
#   Rough cost (depth 5, budget 3000, all turns, this box): ~0.6 s/game burn,
#   ~1.3 slivers, ~2.1 antilife -> ~1700-6300 games/hr; multiply by GAMES x #SEEDS.
#
# Examples:
#   scripts/mine_heuristics.sh decks/slivers_vial.txt
#   GAMES=1000 SEEDS="2002 3003 4004" scripts/mine_heuristics.sh decks/NewDeck.cod
set -euo pipefail

DECK="${1:?usage: mine_heuristics.sh <deckfile> [profile] [games] [seed] [depth] [budget] [turn]}"
PROF="${2:-${DECK%.*}.profile.json}"
GAMES="${3:-${GAMES:-500}}"
DEPTH="${5:-${DEPTH:-5}}"
BUDGET="${6:-${BUDGET:-3000}}"
TURN="${7:-${TURN:-0}}"
# Single-seed positional/env override; else the SEEDS list (multi-seed = overfit guard).
if [ -n "${4:-}" ]; then SEEDS="$4"; elif [ -n "${SEED:-}" ]; then SEEDS="$SEED"; else SEEDS="${SEEDS:-2002 3003}"; fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$HERE/build/Release/mtg"; [ -f "$BIN" ] || BIN="$HERE/build/Release/mtg.exe"
[ -f "$BIN" ] || { echo "ERROR: $BIN not found -- cmake --build build --config Release first." >&2; exit 1; }
[ -f "$DECK" ] || { echo "ERROR: deck $DECK not found." >&2; exit 1; }
[ -f "$PROF" ] || { echo "ERROR: profile $PROF not found (analyze the deck / pass profile arg)." >&2; exit 1; }

OUTDIR="$HERE/logs/heuristics"
mkdir -p "$OUTDIR"
name="$(basename "${DECK%.*}")"
DUMP="$OUTDIR/${name}.ewins.jsonl"
: > "$DUMP"   # truncate; append each seed's decisions

echo "[mine] deck=$DECK games=$GAMES/seed seeds='$SEEDS' depth=$DEPTH budget=$BUDGET turn=${TURN} -> $DUMP" >&2
for s in $SEEDS; do
    echo "[mine]   seed $s ..." >&2
    MTG_DUMP_EWINS=1 MTG_SEARCH_ORDER=1 MTG_DUMP_EWINS_TURN="$TURN" \
        "$BIN" "$DECK" --profile "$PROF" --seed "$s" --games "$GAMES" \
        --depth "$DEPTH" --budget-ms "$BUDGET" 2>>"$DUMP" >/dev/null
done

echo "[mine] $(grep -c '"ewins"' "$DUMP") decisions dumped across seeds: $SEEDS" >&2
python3 "$HERE/scripts/analyze_earliest_wins.py" "$DUMP"
