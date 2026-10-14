#!/usr/bin/env bash
# Build this worktree (Release) through the two-slot lock. Usage: build.sh <tag>
# Copies the resulting binaries to build/scratch/bp_open/bin/<tag>/ so arms can be pinned.
set -u
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
OUT=/workspaces/MagicDeckTester/build/scratch/bp_open
TAG=${1:-cur}
mkdir -p "$OUT"
cd "$WT"
bash /workspaces/MagicDeckTester/build/scratch/build2.sh > "$OUT/build_$TAG.log" 2>&1
rc=$?
echo "build rc=$rc" >> "$OUT/build_$TAG.log"
if [ $rc -eq 0 ]; then
  mkdir -p "$OUT/bin/$TAG"
  cp -f "$WT/build/Release/mtg" "$OUT/bin/$TAG/mtg"
  if [ -f "$WT/build/Release/mtg-test" ]; then cp -f "$WT/build/Release/mtg-test" "$OUT/bin/$TAG/mtg-test"; fi
  git -C "$WT" rev-parse HEAD > "$OUT/bin/$TAG/HEAD"
  git -C "$WT" diff --stat >> "$OUT/bin/$TAG/HEAD"
fi
exit $rc
