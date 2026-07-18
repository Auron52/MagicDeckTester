#!/usr/bin/env bash
# A/B the escalation partial-budget-restore lever (MTG_ESCALATION_FRESH_FRAC) on the value-model decks.
# Deterministic: LP from won+avg (losses = max_turns+1 = 9); rollout_steps = deterministic work proxy.
set -u
MTG=build/Release/mtg
G=${G:-200}; SEED=${SEED:-1001}; PEN=9
run() {  # $1=deck $2=label $3..=env
  local deck="$1"; local label="$2"; shift 2
  local out won avg roll lp
  out=$(env MTG_ROLLOUT_STATS=1 "$@" $MTG "$deck" --depth 5 --budget-ms 20 --max-turns 8 \
        --games "$G" --seed "$SEED" --lookahead-bottoming --threads 0 2>/tmp/rs.$$)
  won=$(echo "$out" | grep -oiE 'won[: ]+[0-9]+' | grep -oE '[0-9]+' | head -1)
  avg=$(echo "$out" | grep -oiE 'avg win turn[: ]+[0-9.]+' | grep -oE '[0-9.]+' | head -1)
  roll=$(grep -oE 'turn_steps=[0-9]+' /tmp/rs.$$ | grep -oE '[0-9]+' | head -1)
  lp=$(awk -v w="$won" -v a="$avg" -v g="$G" -v p="$PEN" 'BEGIN{printf "%.4f",(w*a+(g-w)*p)/g}')
  printf "  %-8s won=%-4s avg=%-8s LP=%s rollout_steps=%s\n" "$label" "$won" "$avg" "$lp" "$roll"
  rm -f /tmp/rs.$$
}
for spec in "HINATA:decks/Hinata2/Hinata2.cod" "ANTILIFE:decks/Anti-Lifegain/Anti-Lifegain.cod"; do
  name="${spec%%:*}"; deck="${spec#*:}"
  echo "###### $name d5 b20 ${G}g s${SEED} (LP penalty=$PEN) ######"
  run "$deck" OFF
  run "$deck" f0.25 MTG_ESCALATION_FRESH_FRAC=0.25
  run "$deck" f0.50 MTG_ESCALATION_FRESH_FRAC=0.5
  run "$deck" f1.00 MTG_ESCALATION_FRESH_FRAC=1.0
done
echo "FRESHFRAC AB COMPLETE"
