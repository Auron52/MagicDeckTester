#!/usr/bin/env bash
# gi137 lever attribution: each new lever off alone, at shipped and at d8 b0. Usage: g137_attr.sh <bin-tag>
set -u
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/logs/bp_open/g137.sh
T=$1
for L in MTG_BP_WAVE_EMPTY MTG_BP_WAVE_UNHOSTED_SITES MTG_BP_M2_WAVE_FIXPOINT MTG_BP_NODE_NESTED_SLOTS; do
  bash "$S" "$T" "ship_no_${L}" -- "$L=0" &
done
wait
for L in MTG_BP_WAVE_EMPTY MTG_BP_M2_WAVE_FIXPOINT; do
  bash "$S" "$T" "d8b0_no_${L}" --depth 8 --budget-ms 0 --ignore-play-profile -- "$L=0" &
done
wait
grep -H "rc=" /workspaces/MagicDeckTester/build/scratch/bp_open/g137/${T}_*no_*.out
