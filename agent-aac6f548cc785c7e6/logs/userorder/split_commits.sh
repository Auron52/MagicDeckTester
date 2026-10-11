#!/usr/bin/env bash
# split_commits.sh -- commit fix 2 (MTG_FB_HOLD_BANKED) and fix 3 (MTG_LINE_FLOAT_TAP_FIRST) as two commits from
# one working tree that holds both. The three files carrying hunks of BOTH (HeuristicArm.h, EngineFlags.h, the
# test file) get a fix-2-only version for the first commit: the b7cfa4f1 file + fix 2's patch (re-run on a
# scratch copy); the full versions are saved first and restored for the second commit.
set -e
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
U=$WT/logs/userorder
S=/workspaces/MagicDeckTester/build/scratch/agent-aac6f548cc785c7e6/split
mkdir -p "$S/full/src/ai" "$S/full/test/unit" "$S/f2/src/ai" "$S/f2/test/unit"
# save the full (fix 2 + fix 3) versions
cp "$WT/src/ai/HeuristicArm.h" "$S/full/src/ai/HeuristicArm.h"
cp "$WT/src/ai/EngineFlags.h" "$S/full/src/ai/EngineFlags.h"
cp "$WT/test/unit/test_dragonstorm_followups.cpp" "$S/full/test/unit/test_dragonstorm_followups.cpp"
# fix-2-only versions: originals + fix 2's patch on a scratch copy (it also needs the other files it edits)
for f in HeuristicArm.h EngineFlags.h Combat.h Combat.cpp AIEngine.cpp TurnSolver.cpp; do
  git -C "$WT" show "b7cfa4f1:src/ai/$f" > "$S/f2/src/ai/$f"
done
PATCH_ROOT="$S/f2" python3 "$U/patch_fb_banked.py"
git -C "$WT" show "b7cfa4f1:test/unit/test_dragonstorm_followups.cpp" > "$S/f2/test/unit/test_dragonstorm_followups.cpp"
cat "$U/test_fb_banked.cpp.txt" >> "$S/f2/test/unit/test_dragonstorm_followups.cpp"
# sanity: fix 2's own files must equal what is in the tree now
for f in Combat.h Combat.cpp AIEngine.cpp TurnSolver.cpp; do
  cmp "$S/f2/src/ai/$f" "$WT/src/ai/$f"
done
# commit 1
cp "$S/f2/src/ai/HeuristicArm.h" "$WT/src/ai/HeuristicArm.h"
cp "$S/f2/src/ai/EngineFlags.h" "$WT/src/ai/EngineFlags.h"
cp "$S/f2/test/unit/test_dragonstorm_followups.cpp" "$WT/test/unit/test_dragonstorm_followups.cpp"
git -C "$WT" add src/ai/HeuristicArm.h src/ai/EngineFlags.h src/ai/Combat.h src/ai/Combat.cpp src/ai/AIEngine.cpp \
    src/ai/TurnSolver.cpp test/unit/test_dragonstorm_followups.cpp
git -C "$WT" commit -q -F "$U/commit_fb.txt"
# commit 2: restore the full versions
cp "$S/full/src/ai/HeuristicArm.h" "$WT/src/ai/HeuristicArm.h"
cp "$S/full/src/ai/EngineFlags.h" "$WT/src/ai/EngineFlags.h"
cp "$S/full/test/unit/test_dragonstorm_followups.cpp" "$WT/test/unit/test_dragonstorm_followups.cpp"
git -C "$WT" add src/ai/HeuristicArm.h src/ai/EngineFlags.h src/ai/ManaPayment.cpp test/unit/test_dragonstorm_followups.cpp
git -C "$WT" commit -q -F "$U/commit_ft.txt"
git -C "$WT" log --oneline -3
git -C "$WT" status --short | grep -v '^??' || true
