#!/usr/bin/env bash
# dryrun_patches.sh -- apply both patch scripts to a scratch COPY of the touched sources (no build) and
# show the resulting diff stat, so the anchors are proven before the real tree is touched.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
D=/workspaces/MagicDeckTester/build/scratch/agent-aac6f548cc785c7e6/dry
mkdir -p "$D/src/ai" "$D/orig/src/ai"
for f in HeuristicArm.h EngineFlags.h Combat.h Combat.cpp AIEngine.cpp TurnSolver.cpp ManaPayment.cpp; do
  cp "$WT/src/ai/$f" "$D/src/ai/$f"
  cp "$WT/src/ai/$f" "$D/orig/src/ai/$f"
done
PATCH_ROOT="$D" python3 "$WT/logs/userorder/patch_fb_banked.py" || exit 1
PATCH_ROOT="$D" python3 "$WT/logs/userorder/patch_float_tap.py" || exit 1
for f in HeuristicArm.h EngineFlags.h Combat.h Combat.cpp AIEngine.cpp TurnSolver.cpp ManaPayment.cpp; do
  printf "%-16s +%s\n" "$f" "$(diff "$D/orig/src/ai/$f" "$D/src/ai/$f" | grep -c '^>')"
done
