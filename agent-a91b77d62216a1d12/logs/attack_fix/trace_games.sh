#!/usr/bin/env bash
# trace_games.sh <deck> <cell> <bin> "<gi:seed:turn> ..."  -- per game: the v1 log dir + the T<turn>
# projection trace (cast lines with Craterhoof / any), into logs/attack_fix/tr/<deck>_<gi>.*
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
deck=$1; cell=$2; bin=$3; shift 3
mkdir -p "$W/logs/attack_fix/tr"
for spec in $*; do
  gi=${spec%%:*}; rest=${spec#*:}; seed=${rest%%:*}; turn=${rest#*:}
  BIN=$bin MTG_DBG_ATKPROJ=$turn "$W/logs/attack_fix/rg.sh" "$deck" "$cell" "$gi" "$seed" "$W/logs/attack_fix/tr/${deck}_${gi}" \
    > "$W/logs/attack_fix/tr/${deck}_${gi}.out" 2> "$W/logs/attack_fix/tr/${deck}_${gi}.err"
done
echo done
