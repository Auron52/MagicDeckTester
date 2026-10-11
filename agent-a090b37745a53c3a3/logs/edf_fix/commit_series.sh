#!/bin/bash
# Create the 6 EDF fix commits from the verified FINAL tree (states 1..6 of commit_state.py), with no
# builds between them (coordinator ruling 2026-10-11: one build + the full unit suite on the final tree).
# usage: commit_series.sh
set -u
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3
L=$WT/logs/edf_fix
MSG=(none msg1_e5 msg2_e1 msg3_e2a msg4_e2b msg5_e3 msg6_e4)
TESTS=(none test_edf_prospective_null test_edf_untap_credit test_edf_prepay_energy test_edf_aura_payer test_edf_route_board_deploy test_edf_goff_verify)
python3 $L/commit_state.py 0 --check || exit 1
for k in 1 2 3 4 5 6; do
  python3 $L/commit_state.py "$k" || exit 1
  git -C "$WT" add CMakeLists.txt src/ai/TurnSolver.cpp src/ai/DecisionProviders.cpp src/ai/HeuristicArm.h \
      test/unit/test_edf_d0_play.cpp "test/unit/${TESTS[$k]}.cpp" || exit 1
  git -C "$WT" commit -q -F "$L/${MSG[$k]}.txt" || exit 1
  echo "state $k committed $(git -C "$WT" log --oneline -1)" | tee -a "$L/commit_series.txt"
done
# The tree after commit 6 must be exactly the verified final tree.
for f in src/ai/TurnSolver.cpp src/ai/DecisionProviders.cpp src/ai/HeuristicArm.h CMakeLists.txt \
         test/unit/test_edf_d0_play.cpp; do
  cmp -s "$WT/$f" "$L/final/$(basename $f)" || { echo "MISMATCH $f" | tee -a "$L/commit_series.txt"; exit 1; }
done
git -C "$WT" status --short | tee -a "$L/commit_series.txt"
