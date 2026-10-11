#!/bin/bash
# USER 10-10: "back up anything that is not backed up" -- push every worktree's unpushed commits and uncommitted work to
# origin backup/<date>/<name> branches (USER: backup branches are fine; delete them once the work lands on the primary).
#  - the primary checkout's tip is pushed as-is (its CI run is a free Linux + Windows compile check during the hold);
#  - every other worktree with unpushed commits or uncommitted edits gets ONE snapshot commit on top of its HEAD (tracked
#    changes + untracked non-ignored files), made through a throwaway copy of its index: the worktree, its index and its
#    branch are untouched. "[skip ci]" in the message keeps ~20 backup branches from each running CI;
#  - the gitignored scripts / notes / commit drafts in the active trees' logs/ go in one parentless commit.
set -u
R=/workspaces/MagicDeckTester
D=2026-10-10
S=$R/build/scratch/backup
mkdir -p $S
: > $S/refspecs.txt
git -C $R worktree list --porcelain | awk '/^worktree /{print $2}' > $S/wts.txt
while read -r W; do
  if [ "$W" = "$R" ]; then name=phase-1-2-deck-analyzer; else name=$(basename "$W"); fi
  head=$(git -C "$W" rev-parse HEAD)
  unpushed=$(git -C "$W" rev-list --count HEAD --not --remotes=origin)
  dirty=$(git -C "$W" status --porcelain | head -n 1)
  if [ "$unpushed" = 0 ] && [ -z "$dirty" ]; then continue; fi
  if [ "$W" = "$R" ]; then
    [ -n "$dirty" ] && echo "WARNING: primary checkout is dirty; only its commits are backed up"
    tip=$head
  else
    idx=$S/index.$name
    cp "$(git -C "$W" rev-parse --path-format=absolute --git-path index)" "$idx"
    GIT_INDEX_FILE=$idx git -C "$W" add -A
    tree=$(GIT_INDEX_FILE=$idx git -C "$W" write-tree)
    br=$(git -C "$W" rev-parse --abbrev-ref HEAD)
    tip=$(git -C "$W" commit-tree "$tree" -p "$head" -m "[skip ci] backup $D: $name (branch $br, HEAD ${head:0:8}) with its uncommitted work

Snapshot only -- the worktree, its index and its branch were not touched. Delete this branch once the work lands on
the primary branch (USER 10-10).")
  fi
  echo "$tip:refs/heads/backup/$D/$name" >> $S/refspecs.txt
  printf '%-40s unpushed=%-3s dirty=%s -> %s\n' "$name" "$unpushed" "$([ -n "$dirty" ] && echo yes || echo no)" "${tip:0:10}"
done < $S/wts.txt

# Gitignored scripts / notes from the active trees (text only; .txt/.json/.tsv/.csv under 64 KB; nothing else).
L=$S/logs-tree
mkdir -p $L
for src in "$R/logs/discard_impl:primary/logs/discard_impl" "$R/logs/userorder:primary/logs/userorder" \
           $(for a in aac6f548cc785c7e6 a97fc601a3891aba8 a91b77d62216a1d12 a090b37745a53c3a3; do
               echo "$R/.claude/worktrees/agent-$a/logs:agent-$a/logs"; done); do
  from=${src%%:*}; to=$L/${src#*:}
  [ -d "$from" ] || continue
  ( cd "$from" && find . -type f \( \( \( -name '*.py' -o -name '*.sh' -o -name '*.md' \) -size -512k \) -o \
      \( \( -name '*.txt' -o -name '*.json' -o -name '*.tsv' -o -name '*.csv' \) -size -64k \) \) -print0 ) |
    ( cd "$from" && xargs -0 -r cp --parents -t "$(mkdir -p "$to" && echo "$to")" )
done
echo "logs snapshot: $(find $L -type f | wc -l) files, $(du -sh $L | cut -f1)"
lidx=$S/index.logs
rm -f "$lidx"
GIT_INDEX_FILE=$lidx git --git-dir=$R/.git --work-tree=$L add -A -f .
ltree=$(GIT_INDEX_FILE=$lidx git --git-dir=$R/.git write-tree)
ltip=$(git --git-dir=$R/.git commit-tree "$ltree" -m "[skip ci] backup $D: gitignored logs/ scripts, notes and commit drafts of the active trees

primary logs/discard_impl + logs/userorder, and the four resumed agents' logs/ (text only, small files).
Delete this branch once the work lands on the primary branch (USER 10-10).")
echo "$ltip:refs/heads/backup/$D/logs-notes" >> $S/refspecs.txt
echo "logs-notes -> ${ltip:0:10}"
echo "--- $(wc -l < $S/refspecs.txt) refspecs in $S/refspecs.txt"
