#!/usr/bin/env bash
# Beam depth probe: for a (deck, depth, budget, games, seed), run beam-ON (bare => per-deck depth-adaptive beam)
# vs beam-OFF (MTG_ESC_BEAM=0), and print LP (avg turns, loss-penalized) + deterministic rollout work
# (turn_steps, contention-free) + escalation rate/short from MTG_HYBRID_STATS. One line each.
#   usage: beam_depth_probe.sh <deck> <depth> <budget_ms> <games> <seed> [on_env]
# on_env: extra env for the ON arm (default empty = bare per-deck beam). Pass e.g. "MTG_ESC_BEAM=20
#   MTG_ESC_BEAM_STATIC=1 MTG_ESC_BEAM_LEAFDEPTH=1" to force the shallow config at a depth where the per-deck
#   path leaves it off (d4). OFF arm is always MTG_ESC_BEAM=0.
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
deck=$1 depth=$2 bud=$3 games=$4 seed=$5; on_env=${6:-}
run() { # $1=label $2=env
  local out; out=$(env $2 MTG_HYBRID_STATS=1 MTG_ROLLOUT_STATS=1 "$BIN" "${F[$deck]}" --profile "${P[$deck]}" \
    --seed "$seed" --games "$games" --depth "$depth" --budget-ms "$bud" --max-turns 8 --threads 0 \
    --ignore-play-profile 2>&1)
  local lp ts dec red rs
  lp=$(echo "$out" | grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+')
  ts=$(echo "$out" | grep -oP 'turn_steps=\K[0-9]+' | head -1)
  dec=$(echo "$out" | grep -oP 'decisions=\K[0-9]+'); red=$(echo "$out" | grep -oP 'redos=\K[0-9]+')
  rs=$(echo "$out"  | grep -oP 'redo_short=\K[0-9]+')
  printf "  %-4s LP=%-7s turn_steps=%-10s esc=%s/%s(%.0f%%) short=%s\n" "$1" "${lp:-NA}" "${ts:-NA}" \
    "${red:-0}" "${dec:-0}" "$(awk -v a=${red:-0} -v b=${dec:-1} 'BEGIN{print b?100*a/b:0}')" "${rs:-0}"
  echo "${ts:-0}" > /tmp/_bdp_$1
}
budlabel=$bud; [ "$bud" = 0 ] && budlabel="UNBOUNDED"
echo "== $deck d$depth budget=$budlabel ${games}g seed $seed =="
run off "MTG_ESC_BEAM=0"
run on  "$on_env"
awk -v on="$(cat /tmp/_bdp_on)" -v off="$(cat /tmp/_bdp_off)" \
  'BEGIN{ if(off>0) printf "  => beam saves %.1f%% turn_steps (on=%d off=%d)\n", 100*(off-on)/off, on, off }'
rm -f /tmp/_bdp_on /tmp/_bdp_off
