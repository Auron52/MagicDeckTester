#!/usr/bin/env bash
# runbatch.sh <name> <manifest> [bin]  -- ONE pooled batch under the contention rules (USER 10-10):
# binary from the build VOLUME (never from the 9p mount), output to the scratch volume, lowprio.sh
# (cores 0-5, nice 19), 6 threads.  Writes $S/<name>/{m.json,out,err,DONE}.
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
S=/workspaces/MagicDeckTester/build/scratch/agent-a91b77d62216a1d12
name=$1; man=$2
BIN=${3:-$W/build/Release/mtg}
D=$S/$name
mkdir -p "$D"
cp "$man" "$D/m.json"
cd "$W"
MTG_DUMP_WINS=1 bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$BIN" --batch "$D/m.json" --threads 6 > "$D/out" 2> "$D/err"
echo "exit=$?" > "$D/DONE"
