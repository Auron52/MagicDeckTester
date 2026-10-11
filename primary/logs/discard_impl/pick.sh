#!/bin/bash
# Cherry-pick the given commits onto the primary branch, auto-resolving ONLY HeuristicArm.h append conflicts
# (cherry_pick_loop.sh); then verify the arm table (enum and kNames aligned) and that no conflict markers remain.
# Usage: pick.sh <commit>...
R=/workspaces/MagicDeckTester
cd $R
export GIT_EDITOR=true
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "primary tree not clean -- refusing"; git status --short | head; exit 1; }
before=$(git rev-parse --short HEAD)
git cherry-pick "$@" > build/scratch/pick.log 2>&1 || bash logs/discard_impl/cherry_pick_loop.sh || exit 1
python3 logs/discard_impl/check_arm_table.py || { echo "ARM TABLE MISALIGNED"; exit 1; }
if git grep -n -E '^(<<<<<<<|>>>>>>>) ' -- src test CMakeLists.txt; then echo "CONFLICT MARKERS LEFT"; exit 1; fi
echo "picked onto $before:"; git log --oneline $before..HEAD
