#!/usr/bin/env bash
# run_mini.sh <mtg> <manifest> <outdir> [ENV=VAL ...]: oracle batch (MTG_MANA_ORACLE=2), keep issue lines only.
set -u
mtg=$1; man=$2; out=$3; shift 3
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply
env "$@" MTG_MANA_ORACLE=2 MTG_PGO=0 "$mtg" --batch "$man" --threads 2 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|DOMINATED-[A-Z]+|UNMODELED|CAPPED|CAPPED-DECISION) seed|^===' > "$out/flags.err"
echo "rc=${PIPESTATUS[0]}" > "$out/DONE"
