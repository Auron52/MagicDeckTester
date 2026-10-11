#!/usr/bin/env bash
# commit_stages.sh -- commit the 8 stage trees in order (A..H) from the generated stage files.
# Light work only (file copies + git). Stage H == the final working tree.
set -eu
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
S=/workspaces/MagicDeckTester/build/scratch/agent-a91b77d62216a1d12
ST=$S/stages
FILES="src/ai/HeuristicArm.h src/ai/TurnSolver.cpp src/ai/DecisionProviders.h src/ai/DecisionProviders.cpp src/ai/LandPlay.cpp CMakeLists.txt test/unit/test_attack_projection.cpp test/unit/test_bruna_sweep.cpp"
# safety copy of the final files
mkdir -p "$S/final_backup"
for f in $FILES; do mkdir -p "$S/final_backup/$(dirname "$f")"; cp "$W/$f" "$S/final_backup/$f"; done
# stage H must equal the working tree
for f in $FILES; do cmp -s "$ST/H/$f" "$W/$f" || { echo "stage H differs from working tree: $f"; exit 3; }; done
cd "$W"
for st in A B C D E F G H; do
  for f in $FILES; do
    if [ -f "$ST/$st/$f" ]; then cp "$ST/$st/$f" "$W/$f"; git add "$f"; fi
  done
  git commit -q -F "$W/logs/attack_fix/msg/$st.txt"
  echo "$st $(git rev-parse --short HEAD) $(git log -1 --format=%s)"
done
# final tree must equal the backup
for f in $FILES; do cmp -s "$S/final_backup/$f" "$W/$f" || { echo "final differs: $f"; exit 4; }; done
git status --short
echo COMMITTED
