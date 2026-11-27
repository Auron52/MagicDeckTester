#!/usr/bin/env bash
# run_gate2.sh <manifest> <outroot> <tag>=<mtg> ...: the Treasure Hunt oracle gate slice (MTG_MANA_ORACLE=2) once per
# binary, back to back in ONE flock hold (the oracle changes are checker-only, so each binary plays the same games;
# arms cannot share a batch because the oracle levers are process-level). Filtered stderr as run_gate.sh; DONE marker.
set -u
man=$1; root=$2; shift 2
mkdir -p "$root"
cd /workspaces/MagicDeckTester/.claude/worktrees/th-resid
for spec in "$@"; do
  tag=${spec%%=*}; mtg=${spec#*=}; out=$root/$tag; mkdir -p "$out"
  MTG_MANA_ORACLE=2 MTG_RSS_CAP_GB=40 MTG_PGO=0 "$mtg" --batch "$man" --threads "$(nproc)" 2>&1 > "$out/batch.out" \
    | grep -E --line-buffered '^\[mana-oracle\] (APPLY-DROP|APPLY-DROP-ORDER|ENUM-GAP|ORACLE-GAP|DROP-COVERED|HIDDEN-INFO|DOMINATED-[A-Z]+|UNMODELED|CAPPED|CAPPED-DECISION) seed|^===|heartbeat|SLOW-GAME' > "$out/flags.err"
  echo "rc=${PIPESTATUS[0]}" > "$out/DONE"
done
echo done > "$root/ALL_DONE"
