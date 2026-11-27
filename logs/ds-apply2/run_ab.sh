#!/usr/bin/env bash
# run_ab.sh <mtg> <manifest> <outdir>: the pooled play A/B (mk_ab.py), MTG_DUMP_WINS=1; stderr filtered to [win] /
# heartbeat / SLOW-GAME lines as it streams. DONE marker. Run it under flock build/scratch/claude_box.lock.
set -u
mtg=$1; man=$2; out=$3
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply2
MTG_DUMP_WINS=1 MTG_RSS_CAP_GB=40 MTG_PGO=0 "$mtg" --batch "$man" --threads "$(nproc)" 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[win\] |heartbeat|SLOW-GAME|^===|rror|bort' > "$out/wins.err"
rc=${PIPESTATUS[0]}
echo "rc=$rc" > "$out/DONE"
