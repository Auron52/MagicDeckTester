#!/usr/bin/env bash
# run_batch.sh <manifest> <outdir> [env assignments...] -- one pooled mtg --batch under the heavy lock (caller wraps)
set -u
cd /workspaces/MagicDeckTester/.claude/worktrees/idgate
M=$1; O=$2; shift 2
mkdir -p "$O/wins"
BIN=${BIN:-build/Release/mtg}
cp "$BIN" "$O/mtg.bin"
export MTG_RSS_CAP_GB=20 MTG_DUMP_UNITS=1
env "$@" "$O/mtg.bin" --batch "$M" --threads "$(nproc)" --game-log-dir "$O/wins" > "$O/out" 2> "$O/err"
echo "rc=$?" > "$O/DONE"
