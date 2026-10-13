#!/bin/bash
# gi1106 repro at three cells. Usage: g1106.sh <binary> [ENV=VAL ...]
# Prints one line per cell: cell, win turn.
BIN=$1; shift
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6
D=decks/FiveColour/FiveColour.cod
P=decks/FiveColour/FiveColour.profile.json
run() {
  local name=$1; shift
  local out
  out=$(env -C $WT "$@" taskset -c 0-5 nice -n 19 $BIN $D --profile $P --seed 6211107 --game-index 1106 --games 1 $CELLARGS 2>&1)
  echo "$name: $(echo "$out" | grep -iE 'avg|won|win turn|Average' | head -3 | tr '\n' ' ')"
}
CELLARGS="--depth 8 --budget-ms 0 --ignore-play-profile" run d8b0 env "$@"
CELLARGS="--budget-ms 20" run ship env "$@"
CELLARGS="--depth 3 --budget-ms 10 --ignore-play-profile" run d3b10 env "$@"
