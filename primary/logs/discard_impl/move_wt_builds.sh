#!/bin/bash
# Move each agent worktree's build/ (regenerable compile output) off the 9p mount onto the ext4 build volume.
# Source and edits stay on C:. The old 9p build dir is renamed aside, not deleted.
set -u
R=/workspaces/MagicDeckTester
V=$R/build/wt
mkdir -p $V $R/build/scratch
# git must ignore the symlink (the build/ pattern matches directories only) and the renamed old dir.
EX=$R/.git/info/exclude
grep -qx '/build' $EX || echo '/build' >> $EX
grep -qx '/build.9p-old/' $EX || echo '/build.9p-old/' >> $EX
for a in aac6f548cc785c7e6 a97fc601a3891aba8 a91b77d62216a1d12 a090b37745a53c3a3; do
  W=$R/.claude/worktrees/agent-$a
  mkdir -p $V/agent-$a $R/build/scratch/agent-$a
  if [ -L $W/build ]; then echo "$a: already a symlink -> $(readlink $W/build)"; continue; fi
  if [ -e $W/build.9p-old ]; then echo "$a: build.9p-old exists, skipping"; continue; fi
  mv $W/build $W/build.9p-old
  ln -s $V/agent-$a $W/build
  echo "$a: $(readlink $W/build)"
  git -C $W status --short | grep -E 'build' || echo "  git status clean of build entries"
done
