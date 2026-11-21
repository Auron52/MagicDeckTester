#!/bin/bash
# bisect_step.sh <idx> [build|play|both]: one Five Colour d0 bisect step (see bisect_fc.sh). Build through the lane at
# 4 slots, play the 6 traced d0 games at 1 slot / 1 thread (they are ~seconds each). Appends to $A/bisect/log.
set -u
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
L=$A/bisect
cd $N
mapfile -t C < <(git rev-list --reverse 1443eeba0..c86a86333 -- src)
i=$1; c=${C[$i]}; what=${2:-both}
if [ "$what" != play ]; then
  sleep 240
  git restore --source="$c" --worktree -- src test/unit CMakeLists.txt cmake
  bash scripts/box_lane.sh --slots ${BSLOTS:-2} --cap-gb 4 -- ./build.sh release -- --target mtg > $L/build_$c.log 2>&1 || { echo "idx=$i $c BUILDFAIL" >> $L/log; exit 1; }
  cp build/Release/mtg $L/mtg_idx$i
fi
rm -rf $L/w_$c
MTG_PGO=0 bash scripts/box_lane.sh --slots 1 --cap-gb 4 -- $L/mtg_idx$i --batch $A/fc_tr.json --threads 1 --game-log-dir $L/w_$c > /dev/null 2>&1
a=$(awk '{print $2}' $L/w_$c/fivecolour_overnight_d0_s4004_gi67_tr.wins)
b=$(awk '{print $2}' $L/w_$c/fivecolour_overnight_d0_s4004_gi192_tr.wins)
r=bad; { [ "$a" = 5 ] && [ "$b" = 5 ]; } && r=good
echo "idx=$i $c gi67=$a gi192=$b -> $r" >> $L/log
echo "$r"
