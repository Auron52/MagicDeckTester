#!/usr/bin/env bash
# Where is the greedy, AT SHIPPED PLAY SETTINGS? One process per deck, results normalised per game.
#
# WHY THIS IS NOT THE FORBIDDEN PER-ITEM LOOP. CLAUDE.md mandates pooling long runs into ONE
# `mtg --batch`. That cannot be done here: every counter this reads (greedysite, m2yield, bp-probe)
# is a PROCESS-GLOBAL atomic dumped by a static destructor at exit, so pooling decks into one batch
# would sum them into a single unattributable total. One process per deck is a genuine data
# dependency, not a scheduling choice -- and the processes run CONCURRENTLY, so the box still sees
# one tail rather than one per deck.
#
# WHY SHIPPED SETTINGS AND NOT budget_ms=0. Diagnosing cost on an unbudgeted run was a real mistake
# on 2026-08-26: at budget_ms=0 the same KittyEquipment game reports 5,183,560 breakpoint encounters
# and 1,293,023 interior second mains; AT d5/b20 IT REPORTS 124,747 AND 1,887. Every conclusion
# drawn from the unbudgeted numbers was about a configuration nobody runs, and the direction was
# WRONG as well as the magnitude -- the shipped search covers LESS of the continuation space
# (29.2% searched) than the unbudgeted one (69.8%). Unbudgeted has exactly one legitimate use here:
# pricing a PRUNE or a conversion without the budget reinvesting the saving. Never for "why".
#
# Usage: bash test/tools/greedy_census.sh [games] [seed] [outdir]
set -u

GAMES="${1:-200}"
SEED="${2:-600001}"
OUT="${3:-logs/greedy_census}"
BIN=build/Release/mtg

[ -x "$BIN" ] || { echo "build first: ./build.sh" >&2; exit 1; }
mkdir -p "$OUT"

# shellcheck disable=SC1091
source test/regression_cases.sh

pids=()
for deck in "${!DECK_FILE[@]}"; do
    (
        MTG_M2_YIELD_STATS=1 MTG_BP_PROBE=1 MTG_BP_CANDS_PROBE=1 \
        "$BIN" "${DECK_FILE[$deck]}" --profile "${DECK_PROF[$deck]}" \
               --games "$GAMES" --seed "$SEED" --threads 1 \
               > "$OUT/$deck.log" 2>&1
    ) &
    pids+=("$!")
done
echo "launched ${#pids[@]} decks x $GAMES games at SHIPPED play settings (seed $SEED)"
for p in "${pids[@]}"; do wait "$p"; done
echo "done -> $OUT"
