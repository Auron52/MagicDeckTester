#!/usr/bin/env bash
# run_batch.sh <binary> <dir-with-manifest.json> <threads<=6> [ENV=VAL ...] : one pooled batch, nice 19.
set -u
BIN=$1; DIR=$2; TH=${3:-4}; shift 3
if [ "$TH" -gt 6 ]; then TH=6; fi
cd /workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8 || exit 1
env MTG_PGO=0 MTG_DUMP_WINS=1 "$@" nice -n 19 "$BIN" --batch "$DIR/manifest.json" --threads "$TH" \
    > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit=$?"
