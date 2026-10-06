#!/usr/bin/env bash
# Remove a git worktree ONLY if it holds no unprotected reference game.
#
#   scripts/safe_worktree_remove.sh <worktree-path> [extra `git worktree remove` flags, e.g. --force]
#
# Use this INSTEAD of a bare `git worktree remove` (CLAUDE.md, REFERENCE GAMES rule). It runs
# scripts/check_references_safe.py on that one path and refuses (exit 1) if any reference under its
# references/ is untracked/modified and not on origin or the references-autosave branch. On
# 2026-10-05 a week of the user's hand-played references was lost with a scratch checkout; there
# is no bypass flag -- protect (commit + push) the files it lists, then re-run.
set -euo pipefail
if [ $# -lt 1 ]; then echo "usage: $0 <worktree-path> [git worktree remove flags]" >&2; exit 2; fi
WT="$1"; shift
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ ! -d "$WT" ]; then
  echo "$WT does not exist; nothing to scan. Use 'git worktree prune' for a stale entry." >&2
  exit 2
fi
if ! python3 "$HERE/check_references_safe.py" "$WT"; then
  echo "REFUSING to remove $WT: it holds reference games that exist nowhere else (listed above)." >&2
  exit 1
fi
git -C "$WT" worktree remove "$@" "$WT"
echo "removed worktree $WT (references verified safe first)"
