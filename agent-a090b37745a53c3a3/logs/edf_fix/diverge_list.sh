#!/bin/bash
# diverge.sh over a list of held-out d0 games for one lever. usage: diverge_list.sh <tag> <LEVER_ENV> <gi...>
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3/logs/edf_fix
tag=$1; lev=$2; shift 2
for gi in "$@"; do
  echo "######## $tag gi=$gi (A = all on, B = $lev=0)"
  bash $W/diverge.sh d0 "$gi" "$((5300001 + gi))" "$tag" "" "$lev=0"
done
