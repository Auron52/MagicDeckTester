#!/bin/bash
# Continue an in-progress cherry-pick sequence, auto-resolving ONLY append conflicts in HeuristicArm.h.
# Stops (exit 1) on any other conflicted file.
R=/workspaces/MagicDeckTester
cd $R
export GIT_EDITOR=true
while :; do
  conflicted=$(git diff --name-only --diff-filter=U)
  if [ -n "$conflicted" ]; then
    for f in $conflicted; do
      case "$f" in
        src/ai/HeuristicArm.h) python3 logs/discard_impl/resolve_arm_appends.py "$f" || exit 1; git add "$f" ;;
        *) echo "STOP: non-append conflict in $f"; exit 1 ;;
      esac
    done
  fi
  if [ -f .git/CHERRY_PICK_HEAD ] || [ -d .git/sequencer ]; then
    out=$(git cherry-pick --continue 2>&1); rc=$?
    echo "$out" | grep -E "^\[|CONFLICT|error" | head -n 5
    if [ $rc -eq 0 ] && [ ! -d .git/sequencer ]; then echo "sequence done"; break; fi
    [ $rc -ne 0 ] && [ -z "$(git diff --name-only --diff-filter=U)" ] && { echo "STOP: continue failed: $out"; exit 1; }
  else
    echo "no cherry-pick in progress"; break
  fi
done
git log --oneline -10
