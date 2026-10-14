#!/usr/bin/env bash
# gi137: each new lever ON alone (the other three =0), plus all four =0, at shipped. Usage: g137_only.sh <bin-tag>
set -u
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/logs/bp_open/g137.sh
T=$1
ALL=(MTG_BP_WAVE_EMPTY MTG_BP_WAVE_UNHOSTED_SITES MTG_BP_M2_WAVE_FIXPOINT MTG_BP_NODE_NESTED_SLOTS)
offs() { local keep=$1; local out=(); for L in "${ALL[@]}"; do [ "$L" = "$keep" ] || out+=("$L=0"); done; echo "${out[@]}"; }
for K in "${ALL[@]}" NONE; do
  # shellcheck disable=SC2046
  bash "$S" "$T" "ship_only_${K}" -- $(offs "$K") &
done
wait
grep -H "rc=" /workspaces/MagicDeckTester/build/scratch/bp_open/g137/${T}_ship_only_*.out
