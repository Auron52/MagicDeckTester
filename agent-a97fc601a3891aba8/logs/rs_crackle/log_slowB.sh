#!/usr/bin/env bash
# Log both arms of every lever-B slower game, 4 lanes in parallel -> logs/rs_crackle/LB/
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
split -n l/4 -d "$R/slowB.txt" "$R/slowB_part"
for p in 00 01 02 03; do
  bash "$R/log_pairs.sh" "$R/slowB_part$p" LB > "$R/slowB_part$p.out" 2>&1 &
done
wait
cat "$R"/slowB_part0?.out > "$R/slowB_runs.out"
wc -l "$R/slowB_runs.out"
