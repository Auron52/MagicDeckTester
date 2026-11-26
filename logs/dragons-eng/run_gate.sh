#!/usr/bin/env bash
# run_gate.sh <mtg> <manifest> <outdir> [oracle-level]: one pooled batch (the dragons-ogap gate slice; arm levers come in
# through the environment -- PHANTOM is a quiet class, counted only in the per-process summary, so each arm is its own batch), stderr filtered as it streams
# (issue-class flag lines kept whole, heartbeats / SLOW-GAME / summaries kept). DONE marker. Run under claude_box.lock.
set -u
mtg=$1; man=$2; out=$3; lvl=${4:-2}
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/dragons-eng
MTG_MANA_ORACLE=$lvl MTG_RSS_CAP_GB=40 MTG_PGO=0 "$mtg" --batch "$man" --threads "$(nproc)" 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|PHANTOM|DOMINATED-[A-Z]+|UNMODELED|CAPPED|CAPPED-DECISION) seed|^===|heartbeat|SLOW-GAME|rror|bort' > "$out/flags.err"
rc=${PIPESTATUS[0]}
echo "rc=$rc" > "$out/DONE"
