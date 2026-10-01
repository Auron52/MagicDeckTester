#!/usr/bin/env bash
# Measure duplicate-branching levers on WORK, not on `units`.
#
# USER 2026-10-01: *"take whatever options we can 'quality-lever' or not to minimize unnecessary
# branching"*, *"We should do exactly the work we need to and no more"*, *"It isn't worth doing work
# we don't need to just to prevent us from searching deeper"*, and -- the one that sets the metric --
# *"If we want to do that, then we should just adjust how we charge against the budget instead (and
# do less work overall)."*
#
# WHY `units` IS THE WRONG READOUT HERE. SearchBudget::Consume charges 1 unit per search node, but a
# node drags 7.6 mana-payment solves on snow and that ratio varies 119x across decisions
# (docs/design/per-decision-work-census.md). So a lever that removes duplicate candidates frees units
# the budget immediately re-spends, and `units` comes back flat while REAL work moved. The columns
# that matter are therefore pay_calls / rollout_calls / CPU, with avg-turns beside them as the
# quality check -- a lever that removes work and loses turns is not a win.
#
# ONE POOLED QUEUE, every arm launched together (CLAUDE.md: waves are a loop). Arms share the box, so
# wall time is contended and only CPU-per-game is comparable across arms; the counter columns are
# deterministic and unaffected.
set -u
DECK=${DECK:-decks/Snow/Snow.cod}
PROF=${PROF:-decks/Snow/Snow.profile.json}
DEPTH=${DEPTH:-3}
BUDGET=${BUDGET:-10}
GAMES=${GAMES:-30}
CHUNKS=${CHUNKS:-11}
BASE=${BASE:-920000}
OUT=${OUT:-logs/leversweep}
BIN=build/Release/mtg

# arm name | env assignments. Override with ARMS_SPEC (one "name|assignments" per line) rather than
# copying this script -- a second copy is how two measurements end up on different seeds, chunk
# counts or settings and stop being comparable.
ARMS=(
  "base|"
  "dedup|MTG_CAND_DEDUP=1"
  "all|MTG_CAND_DEDUP=1 MTG_BP_ARM_NEW=1 MTG_BP_NSKIP_GLOBAL=2 MTG_BP_NSKIP_ATPLAY=1 MTG_SNOW_LOOK_COLOR=1"
)
if [ -n "${ARMS_SPEC:-}" ]; then
    mapfile -t ARMS <<< "$ARMS_SPEC"
fi

mkdir -p "$OUT"
pids=()
for spec in "${ARMS[@]}"; do
    arm=${spec%%|*}; envs=${spec#*|}
    mkdir -p "$OUT/$arm"
    for ((c=0; c<CHUNKS; ++c)); do
        seed=$((BASE + c * GAMES))
        # shellcheck disable=SC2086
        env $envs MTG_ROLLOUT_STATS=1 "$BIN" "$DECK" --profile "$PROF" \
            --games "$GAMES" --seed "$seed" --depth "$DEPTH" --budget-ms "$BUDGET" --threads 1 \
            > "$OUT/$arm/chunk_$c.log" 2>&1 &
        pids+=($!)
    done
done
echo "launched ${#pids[@]} processes: ${#ARMS[@]} arms x $CHUNKS chunks x $GAMES games"
echo "seeds $BASE .. $((BASE + CHUNKS * GAMES - 1))  -> $OUT"
for p in "${pids[@]}"; do wait "$p"; done
echo "done"
