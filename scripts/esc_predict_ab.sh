#!/usr/bin/env bash
# A/B the escalation K-predictor (MTG_ESC_PREDICT, ladder-start-gate REPLAY) vs the baseline full ladder.
# Deterministic work = interior_nodes + turn_steps (contention-proof; wall-clock is not). LP = loss-penalized
# avg win turn (losses = max_turns+1). Byte-identical committed lines => identical won/avg (LP delta 0).
set -u
MTG=build/Release/mtg
DECK=${DECK:-decks/Hinata2/Hinata2.cod}
DEPTH=${DEPTH:-5}; BMS=${BMS:-20}; MT=${MT:-8}
G=${G:-120}; SEED=${SEED:-1001}; PEN=$((MT+1))
run() {  # $1=label $2..=env
  local label="$1"; shift
  local out won avg roll interior work lp
  out=$(env MTG_ROLLOUT_STATS=1 "$@" $MTG "$DECK" --depth $DEPTH --budget-ms $BMS --max-turns $MT \
        --games "$G" --seed "$SEED" --lookahead-bottoming --threads 0 2>/tmp/ep.$$)
  won=$(echo "$out" | grep -oiE 'won[: ]+[0-9]+' | grep -oE '[0-9]+' | head -1)
  avg=$(echo "$out" | grep -oiE 'avg win turn[: ]+[0-9.]+' | grep -oE '[0-9.]+' | head -1)
  roll=$(grep -oE 'turn_steps=[0-9]+' /tmp/ep.$$ | grep -oE '[0-9]+' | head -1)
  interior=$(grep -oE 'interior_nodes=[0-9]+' /tmp/ep.$$ | grep -oE '[0-9]+' | head -1)
  work=$(( ${interior:-0} + ${roll:-0} ))
  lp=$(awk -v w="$won" -v a="$avg" -v g="$G" -v p="$PEN" 'BEGIN{printf "%.4f",(w*a+(g-w)*p)/g}')
  printf "  %-24s won=%-4s avg=%-8s LP=%-8s work=%-10s (interior=%s rollout=%s)\n" \
         "$label" "$won" "$avg" "$lp" "$work" "$interior" "$roll"
  rm -f /tmp/ep.$$
}
echo "###### $(basename $DECK) d$DEPTH b$BMS ${G}g s${SEED} (work=interior+rollout; LP pen=$PEN) ######"
run "baseline(ladder)"
run "predict(replay)"      MTG_ESC_PREDICT=1
echo "ESC PREDICT AB COMPLETE"
