#!/usr/bin/env bash
# STAGE 1 REACHABILITY CENSUS: does hosting the node at the other two DEFERRED breakpoint classes
# (MTG_BP_NODE_D56, sites 5 and 6) actually remove the in-tree greedy fallback there?
#
# The number that matters is greedysite `act[]` -- how often a greedy Solve DECIDED something --
# together with the per-site `why` breakdown (masked / base / nested / overrun). VOLUME IS NOT
# HARM: a greedy call on a state with no legal option returns an empty plan and changes nothing,
# which is why `g[]` alone has misled this arc five times.
#
# ONE PROCESS PER CELL for the same reason scripts/units_probe.sh gives: the greedysite counters
# are process-global atomics dumped at exit, so a pooled batch would report the SUM across arms.
# The cells run concurrently under xargs -P, so this is still ONE saturated pool, not a serial loop.
#
# depth/budget are OMITTED so each deck resolves its own value_play lock -- "at play settings".
set -u

BIN=./build/Release/mtg
OUT=${OUT:-logs/stage1/census}
GAMES=${GAMES:-60}
SEED=${SEED:-7700001}
JOBS=${JOBS:-24}
mkdir -p "$OUT"

# arm -> env assignments. `base` = shipped defaults (no node at all).
declare -A ARMS=(
  [base]=""
  [node]="MTG_BP_NODE=1"
  [d56]="MTG_BP_NODE=1 MTG_BP_NODE_D56=1"
  [node0]="MTG_BP_NODE=1 MTG_BP_NODE_D0ONLY=1"
  [d560]="MTG_BP_NODE=1 MTG_BP_NODE_D0ONLY=1 MTG_BP_NODE_D56=1"
)
ARM_ORDER=(base node d56 node0 d560)

# The decks with in-tree greedy (the others have none -- their s90 leaf is out of scope).
# mirrorwing owns site 5 and kitty site 6, so those two are the decks this lever can move at all;
# the rest are controls that must come back byte-identical between `node` and `d56`.
DECKS=(
 "mirrorwing|decks/Mirrorwing Dragon/Mirrorwing Dragon.cod|decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"
 "kitty|decks/KittyEquipment/KittyEquipment.cod|decks/KittyEquipment/KittyEquipment.profile.json"
 "hinata|decks/Hinata2/Hinata2.cod|decks/Hinata2/Hinata2.profile.json"
 "th|decks/treasure_hunt/treasure_hunt.txt|decks/treasure_hunt/treasure_hunt.profile.json"
 "auras|decks/Auras/Auras.cod|decks/Auras/Auras.profile.json"
 "burn|decks/burn/burn.txt|decks/burn/burn.profile.json"
 "dragonstorm|decks/Dragonstorm/Dragonstorm.cod|decks/Dragonstorm/Dragonstorm.profile.json"
 "antilife|decks/Anti-Lifegain/Anti-Lifegain.cod|decks/Anti-Lifegain/Anti-Lifegain.profile.json"
 "creature_giving|decks/Creature Giving/Creature Giving.cod|decks/Creature Giving/Creature Giving.profile.json"
)

run_cell() {
  # NOT tab-separated: tab is an IFS *whitespace* character, so consecutive tabs collapse and the
  # `base` arm (empty env string) silently shifts every later field -- it ran `env decks/Mirrorwing`
  # and died on the space in the deck name. A non-whitespace delimiter keeps empty fields.
  IFS='^' read -r key arm envs deck prof <<< "$1"
  # shellcheck disable=SC2086
  env $envs MTG_M2_YIELD_STATS=1 "$BIN" "$deck" --profile "$prof" \
      --games "$GAMES" --seed "$SEED" --threads 1 \
      > "$OUT/$key.$arm.out" 2> "$OUT/$key.$arm.err"
  echo "[census] done $key/$arm"
}
export -f run_cell
export BIN OUT GAMES SEED

if [ "${1:-}" = "--collect" ]; then
  for entry in "${DECKS[@]}"; do
    IFS='|' read -r key _ _ <<< "$entry"
    echo "=== $key ==="
    for arm in "${ARM_ORDER[@]}"; do
      f="$OUT/$key.$arm.err"
      [ -f "$f" ] || { printf '  %-6s (missing)\n' "$arm"; continue; }
      avg=$(grep -oE 'avg[_ ]?(win_)?turn[= ]+[0-9.]+' "$OUT/$key.$arm.out" | tail -1)
      # `acted` total across in-tree sites 0-8 (site 90 is the horizon leaf -- out of scope).
      acted=$(awk '/GREEDY SITES/{f=1} f&&/^ *site +[0-8] /{s+=$NF} END{print s+0}' "$f")
      printf '  %-6s acted(s0-8)=%-10s %s\n' "$arm" "$acted" "$avg"
      awk '/GREEDY SITES/{f=1} f' "$f" | sed -n '2,40p' | sed 's/^/      /'
    done
  done
  exit 0
fi

for entry in "${DECKS[@]}"; do
  IFS='|' read -r key deck prof <<< "$entry"
  for arm in "${ARM_ORDER[@]}"; do
    printf '%s^%s^%s^%s^%s\n' "$key" "$arm" "${ARMS[$arm]}" "$deck" "$prof"
  done
done | xargs -d '\n' -P "$JOBS" -I{} bash -c 'run_cell "$@"' _ {}
