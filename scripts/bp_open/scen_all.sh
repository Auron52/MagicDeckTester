#!/usr/bin/env bash
# Whole scenario suite on a pinned binary, from the worktree root. Usage: scen_all.sh <bin-tag> [ENV=VAL ...]
set -u
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
OUT=/workspaces/MagicDeckTester/build/scratch/bp_open/scen
T=$1; shift
mkdir -p "$OUT"
cd "$WT"
env MTG_PGO=0 "$@" MTG_BIN=/workspaces/MagicDeckTester/build/scratch/bp_open/bin/$T/mtg nice -n 10 bash test/scenarios.sh > "$OUT/$T.txt" 2>&1
tail -1 "$OUT/$T.txt"
