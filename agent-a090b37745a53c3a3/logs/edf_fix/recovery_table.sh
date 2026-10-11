#!/bin/bash
# Recovery table: each listed game, on one binary, under: all levers on / each lever ALONE on (the other
# four =0, i.e. the tip plus that lever) / all five =0 (the tip's behaviour). Prints one row per game.
# usage: recovery_table.sh <binary> <games-file>
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3/logs/edf_fix
S=/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/rt
BIN=$1; LIST=$2
L=(MTG_SOLVE_UNTAP_CREDIT MTG_PREPAY_ENERGY_RESTORE MTG_SOLVE_AURA_PAYER_CHECK MTG_EDF_ROUTE_BOARD_DEPLOY MTG_EDF_GOFF_VERIFY_UNPROJECTED)
TAGS=(L1 L2a L2b L3 L4)
turns() {   # <cell> <gi> <seed> <tag> <env...>
  local cell=$1 gi=$2 seed=$3 tag=$4; shift 4
  ENV_EXTRA="$*" bash $W/run1.sh "$cell" "$gi" "$seed" new "$S/${tag}_g${gi}_${cell}" "$BIN" \
    | grep -o "avg (turns)   : [0-9.]*" | awk '{printf "%d", $NF}'
}
printf "%-5s %-6s %-6s %-6s %-6s %-6s %-6s %-6s\n" cell gi allon L1 L2a L2b L3 L4
while read -r cell gi seed; do
  [ -z "$gi" ] && continue
  on=$(turns "$cell" "$gi" "$seed" allon)
  row="$on"
  for i in 0 1 2 3 4; do
    env=""
    for k in 0 1 2 3 4; do [ $k -ne $i ] && env="$env ${L[$k]}=0"; done
    row="$row $(turns "$cell" "$gi" "$seed" "only${TAGS[$i]}" $env)"
  done
  off=""; for k in 0 1 2 3 4; do off="$off ${L[$k]}=0"; done
  row="$row $(turns "$cell" "$gi" "$seed" alloff $off)"
  set -- $row
  printf "%-5s %-6s %-6s %-6s %-6s %-6s %-6s %-6s alloff=%s\n" "$cell" "$gi" "$1" "$2" "$3" "$4" "$5" "$6" "$7"
done < "$LIST"
