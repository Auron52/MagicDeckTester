#!/usr/bin/env bash
# run_ab.sh <mtg> <manifest> <outdir> [threads]: the pooled play A/B (mk_ab.py) in the small-job lane (run it under
# scripts/box_lane.sh --slots 2 --cap-gb 3), MTG_DUMP_WINS=1; stderr filtered to [win] / heartbeat / SLOW-GAME lines.
# DONE marker <outdir>/DONE.
set -u
mtg=$1; man=$2; out=$3; th=${4:-2}
mkdir -p "$out"
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply5
MTG_DUMP_WINS=1 MTG_PGO=0 "$mtg" --batch "$man" --threads "$th" 2>&1 > "$out/batch.out" \
  | grep -E --line-buffered '^\[win\] |heartbeat|SLOW-GAME|^===|rror|bort' > "$out/wins.err"
rc=${PIPESTATUS[0]}
echo "rc=$rc" > "$out/DONE"
