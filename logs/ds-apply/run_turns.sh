#!/usr/bin/env bash
# run_turns.sh <mtg> <outdir> [ENV=VAL ...]: the event games, per turn group, oracle on that turn only. DONE marker.
set -u
mtg=$1; out=$2; shift 2
S=/workspaces/MagicDeckTester/build/scratch/ds-apply
mkdir -p "$out"; : > "$out/flags.err"
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply
for m in $S/miniT*.json; do
  t=$(basename $m .json); t=${t#miniT}
  env "$@" MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_TURN=$t MTG_PGO=0 "$mtg" --batch "$m" --threads 2 2>&1 >> "$out/batch.out" \
    | grep -E '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|UNMODELED|CAPPED|CAPPED-DECISION) seed|^=== MANA ORACLE:' >> "$out/flags.err"
done
echo done > "$out/DONE"
