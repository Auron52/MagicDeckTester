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
#     GAMES    default 60
#     SEED     default 2002
#     DEPTH    default 5
#     BUDGET   default 3000 (virtual-ms per decision)
#     TURN     default 0 = mine EVERY pre-combat decision; or a single turn number
#
# Examples:
#   scripts/mine_heuristics.sh decks/slivers_vial.txt
#   GAMES=200 scripts/mine_heuristics.sh decks/NewDeck.cod decks/NewDeck.profile.json
set -euo pipefail

DECK="${1:?usage: mine_heuristics.sh <deckfile> [profile] [games] [seed] [depth] [budget] [turn]}"
PROF="${2:-${DECK%.*}.profile.json}"
GAMES="${3:-${GAMES:-60}}"
SEED="${4:-${SEED:-2002}}"
DEPTH="${5:-${DEPTH:-5}}"
BUDGET="${6:-${BUDGET:-3000}}"
TURN="${7:-${TURN:-0}}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$HERE/build/Release/mtg"; [ -f "$BIN" ] || BIN="$HERE/build/Release/mtg.exe"
[ -f "$BIN" ] || { echo "ERROR: $BIN not found -- cmake --build build --config Release first." >&2; exit 1; }
[ -f "$DECK" ] || { echo "ERROR: deck $DECK not found." >&2; exit 1; }
[ -f "$PROF" ] || { echo "ERROR: profile $PROF not found (analyze the deck / pass profile arg)." >&2; exit 1; }

OUTDIR="$HERE/logs/heuristics"
mkdir -p "$OUTDIR"
name="$(basename "${DECK%.*}")"
DUMP="$OUTDIR/${name}.ewins.jsonl"

echo "[mine] deck=$DECK games=$GAMES seed=$SEED depth=$DEPTH budget=$BUDGET turn=${TURN} -> $DUMP" >&2
MTG_DUMP_EWINS=1 MTG_SEARCH_ORDER=1 MTG_DUMP_EWINS_TURN="$TURN" \
    "$BIN" "$DECK" --profile "$PROF" --seed "$SEED" --games "$GAMES" \
    --depth "$DEPTH" --budget-ms "$BUDGET" 2>"$DUMP" >/dev/null

echo "[mine] $(grep -c '"ewins"' "$DUMP") decisions dumped" >&2
python3 "$HERE/scripts/analyze_earliest_wins.py" "$DUMP"
