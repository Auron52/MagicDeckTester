#!/bin/bash
# Exercise build.sh's 9p guard on a throwaway no-checkout worktree (no compile: the guard block is run alone).
set -u
R=/workspaces/MagicDeckTester
T=$R/.claude/worktrees/buildsh-guard-test
git -C $R worktree add --detach --no-checkout $T HEAD >/dev/null 2>&1 || { echo "worktree add failed"; exit 1; }
mkdir -p $T/build
echo old > $T/build/marker
# The guard block alone, cut from build.sh between its first comment line and the PGO comment.
sed -n '/^# Compile output stays OFF the 9p host mount/,/^# PGO+LTO mode/p' $R/build.sh | sed '$d' > $T/guard.sh
cd $T
bash -euo pipefail guard.sh
echo "--- after first run"
ls -la $T | grep -E 'build' ; readlink $T/build; cat $T/build.9p-old/marker
git -C $T status --short --ignored | grep -E 'build' || echo "(git shows no build entries)"
echo "--- second run is a no-op"
bash -euo pipefail guard.sh; echo "exit $?"
echo "--- primary checkout is untouched"
cd $R
bash -euo pipefail $T/guard.sh; echo "exit $?"; ls -ld $R/build
