#!/usr/bin/env bash
# vbatch.sh <binary-on-volume> <dir-on-volume-with-manifest.json> <threads<=6> [ENV=VAL ...]
# One pooled batch under the contention rules (lowprio.sh: cores 0-5, nice 19), wins dumped.
set -u
BIN=$1; DIR=$2; TH=${3:-6}; shift 3
if [ "$TH" -gt 6 ]; then TH=6; fi
cd /workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8 || exit 1
env MTG_DUMP_WINS=1 "$@" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$BIN" --batch "$DIR/manifest.json" --threads "$TH" \
    > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit=$?"
