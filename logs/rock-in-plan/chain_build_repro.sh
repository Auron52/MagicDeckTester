#!/bin/bash
# usage: chain_build_repro.sh <tag> [manifest]   -- run under the box lock: build, unit filter, then the repro hold.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan
TAG="$1"; MAN="$2"
cd "$W" || exit 1
echo "build start $(date -u)" > "$S/$TAG.status"
if ! ./build.sh > "$S/$TAG.build.log" 2>&1; then
  echo "BUILD FAILED $(date -u)" >> "$S/$TAG.status"; echo FAIL > "$S/$TAG.done"; exit 1
fi
echo "build ok $(date -u)" >> "$S/$TAG.status"
build/Release/mtg-test -tc="Line keep*" > "$S/$TAG.units_lk.txt" 2>&1
echo "units_lk rc=$? $(date -u)" >> "$S/$TAG.status"
bash "$W/logs/rock-in-plan/run_repro.sh" "$TAG" "$MAN"
echo "repro done $(date -u)" >> "$S/$TAG.status"
