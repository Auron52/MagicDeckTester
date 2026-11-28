#!/usr/bin/env bash
# run_gate.sh <mtg> <manifest> <outdir> [threads]: one pooled oracle batch (MTG_MANA_ORACLE=2) in the small-job lane
# (run it under scripts/box_lane.sh --slots 2 --cap-gb 3, which sets MTG_RSS_CAP_GB). stderr filtered as it streams:
# issue-class flag lines kept whole, heartbeats / SLOW-GAME kept. DONE marker <outdir>/DONE.
set -u
mtg=$1; man=$2; out=$3; th=${4:-2}
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply5
MTG_MANA_ORACLE=2 MTG_PGO=0 "$mtg" --batch "$man" --threads "$th" 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|DOMINATED-[A-Z]+|UNMODELED|CAPPED|CAPPED-DECISION) seed|^===|heartbeat|SLOW-GAME' > "$out/flags.err"
rc=${PIPESTATUS[0]}
echo "rc=$rc" > "$out/DONE"
