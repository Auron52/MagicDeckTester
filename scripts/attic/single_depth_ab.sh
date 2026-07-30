#!/usr/bin/env bash
# Single-depth budget-limited escalation A/B, at each deck's SHIPPED config (bare run -> value_play drives).
# Compares the current escalation LADDER (baseline) vs the ADOPTED single-pass path: PREDICTED-affordable
# depth capped at `cap`, with budget FALLBACK. The single arm's env set is byte-identical to the per-deck
# value_play.escalation_cap=cap adoption (MTG_ESC_SINGLE + _PREDICT + _ABS=cap + _FALLBACK). Reports, per arm:
# LP (avg turns, loss-penalized), deterministic
# rollout work (turn_steps, contention-free via MTG_ROLLOUT_STATS), and the escalation ACHIEVED-depth
# histogram (h<d>: count via MTG_HYBRID_STATS) -- the LATTER is the anti-confound guard: only compare work
# when the achieved-depth mix is close. Both arms keep the shipped beam (single-depth composes with it).
#   usage: single_depth_ab.sh <deck> <games> <seed> [cap]
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
deck=$1 games=$2 seed=$3 cap=${4:-3}
run() { # $1=label $2=env
  local out; out=$(env $2 MTG_HYBRID_STATS=1 MTG_ROLLOUT_STATS=1 "$BIN" "${F[$deck]}" --profile "${P[$deck]}" \
    --seed "$seed" --games "$games" --max-turns 8 --threads 0 2>&1)
  local lp ts red dec
  lp=$(echo "$out"  | grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+')
  ts=$(echo "$out"  | grep -oP 'turn_steps=\K[0-9]+' | head -1)
  red=$(echo "$out" | grep -oP 'redos=\K[0-9]+' | head -1)
  dec=$(echo "$out" | grep -oP 'decisions=\K[0-9]+' | head -1)
  local hh; hh=$(echo "$out" | grep -oP '^\s+h[0-9]+: [0-9]+' | tr -s ' ' | paste -sd' ' -)
  printf "  %-9s LP=%-8s turn_steps=%-10s redos=%s/%s  hhist: %s\n" "$1" "${lp:-NA}" "${ts:-NA}" \
    "${red:-0}" "${dec:-0}" "$hh"
  echo "${ts:-0}" > /tmp/_sd_$1
}
echo "== $deck ${games}g seed $seed  (single cap=$cap, fallback on) =="
run ladder ""
run single  "MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_PREDICT=1 MTG_ESC_SINGLE_ABS=$cap MTG_ESC_SINGLE_FALLBACK=1"
awk -v s="$(cat /tmp/_sd_single)" -v l="$(cat /tmp/_sd_ladder)" \
  'BEGIN{ if(l>0) printf "  => single does %.1f%% of ladder work (single=%d ladder=%d)\n", 100*s/l, s, l }'
rm -f /tmp/_sd_single /tmp/_sd_ladder
