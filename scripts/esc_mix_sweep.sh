#!/usr/bin/env bash
# Focused sweep: keep the value-leaf probe FULL (capping it dropped quality to the no-esc floor), and fund a
# SINGLE-DEPTH heuristic escalation with a small restore. Compare vs OFF, no-esc floor, and full-ladder restore.
# Goal: keep the escalation's ~0.02-0.03t quality (LP<=~6.08) without badly degrading rollout work (<=OFF 8.80M).
set -u
MTG=build/Release/mtg
DECK=${DECK:-decks/Hinata2/Hinata2.cod}
G=${G:-200}; SEED=${SEED:-1001}; PEN=9
run() {  # $1=label $2..=env
  local label="$1"; shift
  local out won avg roll lp
  out=$(env MTG_ROLLOUT_STATS=1 "$@" $MTG "$DECK" --depth 5 --budget-ms 20 --max-turns 8 \
        --games "$G" --seed "$SEED" --lookahead-bottoming --threads 0 2>/tmp/ms.$$)
  won=$(echo "$out" | grep -oiE 'won[: ]+[0-9]+' | grep -oE '[0-9]+' | head -1)
  avg=$(echo "$out" | grep -oiE 'avg win turn[: ]+[0-9.]+' | grep -oE '[0-9.]+' | head -1)
  roll=$(grep -oE 'turn_steps=[0-9]+' /tmp/ms.$$ | grep -oE '[0-9]+' | head -1)
  lp=$(awk -v w="$won" -v a="$avg" -v g="$G" -v p="$PEN" 'BEGIN{printf "%.4f",(w*a+(g-w)*p)/g}')
  printf "  %-30s won=%-4s avg=%-8s LP=%-8s rollout=%s\n" "$label" "$won" "$avg" "$lp" "$roll"
  rm -f /tmp/ms.$$
}
echo "###### Hinata d5 b20 ${G}g s${SEED} (LP pen=$PEN; rollout=work proxy) ######"
run "OFF(baseline)"
run "noesc(floor)"                  MTG_VALUE_MIN_DEPTH=0
run "fresh1.0-ladder(ref)"          MTG_ESCALATION_FRESH_FRAC=1.0
run "fresh0.5+single_off0"          MTG_ESCALATION_FRESH_FRAC=0.5 MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_OFFSET=0
run "fresh0.5+single_off1"          MTG_ESCALATION_FRESH_FRAC=0.5 MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_OFFSET=1
run "fresh0.5+single_off2(xover)"   MTG_ESCALATION_FRESH_FRAC=0.5 MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_OFFSET=2
run "fresh0.3+single_off0"          MTG_ESCALATION_FRESH_FRAC=0.3 MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_OFFSET=0
run "fresh0.3+single_off1"          MTG_ESCALATION_FRESH_FRAC=0.3 MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_OFFSET=1
run "fresh1.0+single_off0"          MTG_ESCALATION_FRESH_FRAC=1.0 MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_OFFSET=0
echo "ESC MIX SWEEP COMPLETE"
