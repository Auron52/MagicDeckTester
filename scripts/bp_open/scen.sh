#!/usr/bin/env bash
# Run one scenario fixture with a pinned binary from the worktree root (cards.json is cwd-relative).
# Usage: scen.sh <bin-tag> <scenario.json> [ENV=VAL ...]
set -u
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
TAG=$1; F=$2; shift 2
cd "$WT"
env MTG_PGO=0 "$@" /workspaces/MagicDeckTester/build/scratch/bp_open/bin/$TAG/mtg --scenario "$F" 2>&1 | grep -E "^scenario:|win_turn|ERROR|error" | head -5
