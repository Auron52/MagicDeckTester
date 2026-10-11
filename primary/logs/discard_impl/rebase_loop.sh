#!/bin/bash
# Rebase the primary branch onto origin, auto-resolving ONLY append conflicts in HeuristicArm.h (upstream slots first,
# then the replayed commit's). Stops (exit 1) on any other conflict, leaving the rebase in progress for inspection.
# Usage: rebase_loop.sh start | continue
R=/workspaces/MagicDeckTester
cd $R
export GIT_EDITOR=true
if [ "${1:-continue}" = start ]; then
  git rebase origin/phase-1-2-deck-analyzer > build/scratch/rebase.log 2>&1
  [ -d .git/rebase-merge ] || [ -d .git/rebase-apply ] || { echo "rebase finished with no conflicts"; git log --oneline -3; exit 0; }
fi
while [ -d .git/rebase-merge ] || [ -d .git/rebase-apply ]; do
  conflicted=$(git diff --name-only --diff-filter=U)
  if [ -z "$conflicted" ]; then
    git rebase --continue >> build/scratch/rebase.log 2>&1 || true
    if [ -z "$(git diff --name-only --diff-filter=U)" ] && { [ -d .git/rebase-merge ] || [ -d .git/rebase-apply ]; }; then
      # still in progress with nothing conflicted: an empty commit or a stop -- show and bail
      cur=$(cat .git/rebase-merge/msgnum 2>/dev/null)/$(cat .git/rebase-merge/end 2>/dev/null)
      tail -n 5 build/scratch/rebase.log; echo "STOP at $cur (no conflict markers)"; exit 1
    fi
    continue
  fi
  for f in $conflicted; do
    case "$f" in
      src/ai/HeuristicArm.h) python3 logs/discard_impl/resolve_arm_appends.py "$f" || { echo "STOP: $f not a pure append"; exit 1; }
                             git add "$f" ;;
      *) echo "STOP: conflict in $f at $(cat .git/rebase-merge/msgnum)/$(cat .git/rebase-merge/end): $(git log -1 --format=%s REBASE_HEAD 2>/dev/null | cut -c1-100)"; exit 1 ;;
    esac
  done
done
echo "rebase done"; git log --oneline -3
echo "behind origin: $(git rev-list --count HEAD..origin/phase-1-2-deck-analyzer)  ahead: $(git rev-list --count origin/phase-1-2-deck-analyzer..HEAD)"
