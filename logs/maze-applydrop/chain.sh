#!/bin/bash
# after build1: units for the new test file, then the fix slice (3 arms x top-100 decisions), then the pay-first slice.
S=/workspaces/MagicDeckTester/build/scratch/claude_maze_applydrop
W=/workspaces/MagicDeckTester/.claude/worktrees/maze-applydrop
while ! grep -q "EXIT=" $S/build1.log 2>/dev/null; do sleep 30; done
grep -q "EXIT=0" $S/build1.log || { echo "build failed" > $S/chain.STOPPED; exit 1; }
cd $W
bash scripts/box_lane.sh --slots 1 -- build/Release/mtg-test -tc="Maze's End rank object*,Maze's End hold-up*,Maze's End F6*" > $S/units.log 2>&1
echo "units rc=$?" >> $S/units.log
bash scripts/box_lane.sh --slots 2 -- python3 $S/slice.py $W/build/Release/mtg $S/fix $S/slice100.json $S/arms_fix.json --threads 2 > $S/fix.log 2>&1
cd /workspaces/MagicDeckTester/.claude/worktrees/batch-1121
bash $W/scripts/box_lane.sh --slots 2 -- python3 $S/slice.py /workspaces/MagicDeckTester/build/scratch/claude_maze_summit/b2/mtg $S/pf $S/slice60.json $S/arms_pf.json --threads 2 --env MTG_MAZE_SUMMIT_PF_ALL3=1 > $S/pf.log 2>&1
touch $S/chain.DONE
