#!/usr/bin/env bash
# Drive the rebase: auto-resolve GT/bench data conflicts by taking the UPSTREAM side
# (during a rebase "--ours" == the branch being rebased onto). Stop on anything else.
set -u
cd "$(git rev-parse --show-toplevel)"
LOG=/tmp/rebase_resolutions.log
GITDIR=$(git rev-parse --git-dir)

in_rebase () { [ -d "$GITDIR/rebase-merge" ] || [ -d "$GITDIR/rebase-apply" ]; }

resolve_data () {   # returns 0 if everything conflicted was data, 1 if code conflicts remain
  local any_left=0 f
  while read -r f; do
    [ -z "$f" ] && continue
    case "$f" in
      test/regression_gt.txt|test/gt_logs/*.wins|test/ref_bench.json)
        if git checkout --ours -- "$f" 2>/dev/null; then
          git add -- "$f"
          echo "UPSTREAM  $f  $(git rev-parse --short REBASE_HEAD 2>/dev/null)" >> "$LOG"
        else
          git rm -f -- "$f" >/dev/null 2>&1 || true
          echo "UPSTREAM-ABSENT  $f  $(git rev-parse --short REBASE_HEAD 2>/dev/null)" >> "$LOG"
        fi
        ;;
      *) any_left=1 ;;
    esac
  done < <(git diff --diff-filter=U --name-only)
  return $any_left
}

while in_rebase; do
  if git diff --diff-filter=U --name-only | grep -q . ; then
    if ! resolve_data; then
      echo "=== MANUAL CONFLICT at $(git rev-parse --short REBASE_HEAD) :: $(git log -1 --format=%s REBASE_HEAD)"
      git diff --diff-filter=U --name-only
      exit 2
    fi
  fi
  out=$(GIT_EDITOR=true git rebase --continue 2>&1)
  if echo "$out" | grep -qi "now empty\|nothing to commit\|no changes"; then
    orig=$(git rev-parse REBASE_HEAD)
    echo "EMPTY-AFTER-RESOLUTION  $(git rev-parse --short REBASE_HEAD)  $(git log -1 --format=%s REBASE_HEAD)" >> "$LOG"
    if ! git commit --allow-empty -C "$orig" >/dev/null 2>&1; then
      echo "FAILED allow-empty"; echo "$out"; exit 3
    fi
    out=$(GIT_EDITOR=true git rebase --continue 2>&1)
  fi
  if ! in_rebase; then
    echo "$out" | tail -3
    echo "=== REBASE FINISHED"
    exit 0
  fi
  if ! git diff --diff-filter=U --name-only | grep -q . ; then
    echo "$out" | tail -25
    echo "=== PAUSED with no conflicted files -- inspect"
    exit 4
  fi
done
echo "=== NOT IN REBASE"
exit 0
