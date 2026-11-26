#!/usr/bin/env bash
# run_gate.sh <mtg> <manifest> <outdir>: one pooled oracle batch (MTG_MANA_ORACLE=2), stderr filtered as it streams:
# issue-class flag lines kept whole, heartbeats / SLOW-GAME / summaries kept, everything else dropped. DONE marker.
set -u
mtg=$1; man=$2; out=$3
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/th-apply
MTG_MANA_ORACLE=2 MTG_RSS_CAP_GB=40 MTG_PGO=0 "$mtg" --batch "$man" --threads "$(nproc)" 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|DOMINATED-[A-Z]+|UNMODELED|CAPPED|CAPPED-DECISION) seed|^===|heartbeat|SLOW-GAME' > "$out/flags.err"
rc=${PIPESTATUS[0]}
echo "rc=$rc" > "$out/DONE"
