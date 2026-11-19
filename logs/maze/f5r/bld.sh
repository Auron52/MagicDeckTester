#!/bin/bash
# usage: bld.sh <tag> [patch]  -- Release build of maze-f5r (+patch), copy binary to scratch mtg.<tag>
# bld.sh chain: base (clean), diag (diag.patch), bound1 (bound1.patch) in one lock hold.
cd /workspaces/MagicDeckTester/.claude/worktrees/maze-f5r
S=/workspaces/MagicDeckTester/build/scratch/maze/f5r
one() {  # tag patch
  if [ -n "$2" ]; then git apply "$2" || { echo "rc=apply-failed" > $S/build.$1.DONE; return; }; fi
  env MTG_BUILD_JOBS=12 ./build.sh > $S/build.$1.log 2>&1
  rc=$?
  if [ $rc -eq 0 ]; then cp -f build/Release/mtg $S/mtg.$1; cp -f build/Release/mtg-test $S/mtg-test.$1 2>/dev/null; fi
  echo "rc=$rc" > $S/build.$1.DONE
  if [ -n "$2" ]; then git apply -R "$2"; fi
}
if [ "$1" = base ]; then
  one base ""
  one diag $S/diag.patch
  one bound1 $S/bound1.patch
else
  one "$1" "$2"
fi
