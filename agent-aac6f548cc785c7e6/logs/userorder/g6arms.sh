#!/usr/bin/env bash
# g6arms.sh <bin> <seed> <gi> <tag> <arm>... -- arms: old oldrit oldopq olduser head headnouser headnorit headnoopq
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
BIN="$1"; seed="$2"; gi="$3"; tag="$4"; shift 4
OLD=(MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0 MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0 MTG_SOLVE_PENDING_AURA_EXACT=0)
for arm in "$@"; do
  case "$arm" in
    old)        envs=("${OLD[@]}" MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_DRAGONSTORM_USER_ORDER=0) ;;
    oldrit)     envs=("${OLD[@]}" MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_DRAGONSTORM_USER_ORDER=0) ;;
    oldopq)     envs=("${OLD[@]}" MTG_PAYABLE_ORDER_RITUALS=0 MTG_DRAGONSTORM_USER_ORDER=0) ;;
    olduser)    envs=("${OLD[@]}" MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0) ;;
    head)       envs=(MTG_NONE=0) ;;
    headnouser) envs=(MTG_DRAGONSTORM_USER_ORDER=0) ;;
    headnorit)  envs=(MTG_PAYABLE_ORDER_RITUALS=0) ;;
    headnoopq)  envs=(MTG_DRAGONSTORM_ORDER_OPAQUE=0) ;;
    *) echo "unknown arm $arm"; continue ;;
  esac
  bash "$U/g6trace.sh" "$BIN" "$seed" "$gi" "$tag" "$arm" "${envs[@]}" "${EXTRA_ENV[@]}"
done
