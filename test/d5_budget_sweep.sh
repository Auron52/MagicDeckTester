#!/usr/bin/env bash
# Depth-5 budget practicality sweep on the now-default commit-the-line engine.
# For each deck, measures BOTH wall-clock cost and win quality (won / avg win turn)
# across a budget sweep at --depth 5, so we can read:
#   - performance: how much does raising the d5 budget cost in wall-clock?
#   - effectiveness: how much win quality does it buy (and where does it plateau)?
#   - correctness: avg win turn should be MONOTONICALLY non-increasing with budget;
#     if it ever rises, that's a search/fidelity red flag, not just noise.
# Same seed + game set at every budget (only --budget-ms changes), --threads 0, so
# the per-budget wall-clock ratio is the practical signal. Output under logs/ (repo
# convention). Tunables: GAMES, SEED, BUDGETS, DECKS via env.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
BIN=./build/Release/mtg
OUT=logs/d5_budget_sweep
mkdir -p "$OUT"

GAMES=${GAMES:-100}
SEED=${SEED:-4004}
BUDGETS=${BUDGETS:-"200 500 1000 2000"}
DECKS=${DECKS:-"burn slivers th"}
declare -A DECK=( [burn]=test_deck.txt [slivers]=slivers_vial.txt [th]=treasure_hunt.txt )
declare -A PROF=( [burn]=test_deck.profile.json [slivers]=slivers_vial.profile.json [th]=treasure_hunt.profile.json )

SUM="$OUT/summary.txt"
: > "$SUM"
echo "d5 budget sweep | engine=commit-the-line (default) | games=$GAMES seed=$SEED | $(date -u +%H:%M:%S)" | tee -a "$SUM"
printf "%-9s %-7s %-9s %-7s %-9s %-8s\n" deck budget wall_s won avg_turn vs_b200 | tee -a "$SUM"

for d in $DECKS; do
  base_t=""
  for b in $BUDGETS; do
    S=$(date +%s)
    out=$("$BIN" "${DECK[$d]}" --profile "${PROF[$d]}" --games "$GAMES" --seed "$SEED" \
          --depth 5 --budget-ms "$b" --threads 0 --lookahead-bottoming 2>"$OUT/${d}_b${b}.err")
    E=$(date +%s)
    wall=$(( E - S ))
    won=$(echo "$out" | grep -i "Games won" | grep -oE "[0-9]+" | head -1)
    avg=$(echo "$out" | grep -i "Avg win" | grep -oE "[0-9.]+" | head -1)
    [ -z "$base_t" ] && base_t=$wall
    if [ "$base_t" -gt 0 ] 2>/dev/null; then mult=$(awk "BEGIN{printf \"%.1fx\", $wall/$base_t}"); else mult="-"; fi
    printf "%-9s %-7s %-9s %-7s %-9s %-8s\n" "$d" "$b" "$wall" "$won" "$avg" "$mult" | tee -a "$SUM"
  done
done
echo "DONE $(date -u +%H:%M:%S)" | tee -a "$SUM"
