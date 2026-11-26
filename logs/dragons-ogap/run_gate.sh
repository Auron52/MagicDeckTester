#!/usr/bin/env bash
# run_gate.sh <mtg> <manifest> <outdir> [oracle-level]: one pooled batch, stderr filtered as it streams (issue-class
# flag lines kept whole, heartbeats / SLOW-GAME / summaries kept). Oracle level 0 = off (identity run). DONE marker.
set -u
mtg=$1; man=$2; out=$3; lvl=${4:-2}
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/dragons-ogap
MTG_MANA_ORACLE=$lvl MTG_RSS_CAP_GB=40 MTG_PGO=0 "$mtg" --batch "$man" --threads "$(nproc)" 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|DOMINATED-[A-Z]+|UNMODELED|CAPPED|CAPPED-DECISION) seed|^===|heartbeat|SLOW-GAME' > "$out/flags.err"
rc=${PIPESTATUS[0]}
echo "rc=$rc" > "$out/DONE"
