#!/bin/bash
# Continuation of bisect_fc.sh: window lo..hi of `git rev-list --reverse 1443eeba0..c86a86333 -- src`
# (C[lo] good, C[hi] bad). Skips non-building commits (tries the next index up, then down). Builds and plays
# through scripts/box_lane.sh (4 slots) beside the big run that holds claude_box.lock.
set -u
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
L=$A/bisect
LANE="bash $N/scripts/box_lane.sh --slots 4 --cap-gb 4 --"
cd $N
mapfile -t C < <(git rev-list --reverse 1443eeba0..c86a86333 -- src)
lo=${1:-59}; hi=${2:-79}
declare -A SKIP=([69]=1)
echo "bisect2 lo=$lo hi=$hi $(date -u +%H:%M)" >> $L/log
test_commit() {   # $1 = commit; echo good|bad|BUILDFAIL
  local c=$1
  sleep 240   # 9p clock lag: the previous build must be older than the restored files' skewed mtime
  git restore --source="$c" --worktree -- src test/unit CMakeLists.txt cmake
  $LANE ./build.sh release -- --target mtg > $L/build_$c.log 2>&1 || { echo BUILDFAIL; return; }
  rm -rf $L/w_$c
  MTG_PGO=0 $LANE build/Release/mtg --batch $A/fc_tr.json --threads 4 --game-log-dir $L/w_$c > /dev/null 2>&1
  local a b
  a=$(awk '{print $2}' $L/w_$c/fivecolour_overnight_d0_s4004_gi67_tr.wins)
  b=$(awk '{print $2}' $L/w_$c/fivecolour_overnight_d0_s4004_gi192_tr.wins)
  echo "$c gi67=$a gi192=$b" >> $L/log
  if [ "$a" = 5 ] && [ "$b" = 5 ]; then echo good; else echo bad; fi
}
while [ $(( hi - lo )) -gt 1 ]; do
  mid=$(( (lo + hi) / 2 ))
  while [ -n "${SKIP[$mid]:-}" ] && [ $mid -lt $(( hi - 1 )) ]; do mid=$((mid+1)); done
  while [ -n "${SKIP[$mid]:-}" ] && [ $mid -gt $(( lo + 1 )) ]; do mid=$((mid-1)); done
  if [ -n "${SKIP[$mid]:-}" ]; then echo "only skipped commits left between lo=$lo hi=$hi" >> $L/log; break; fi
  r=$(test_commit ${C[$mid]})
  echo "step lo=$lo hi=$hi mid=$mid ${C[$mid]} -> $r" >> $L/log
  if [ "$r" = good ]; then lo=$mid; elif [ "$r" = bad ]; then hi=$mid; else SKIP[$mid]=1; fi
done
echo "FIRST BAD (bisect2): idx=$hi ${C[$hi]} (last good idx=$lo ${C[$lo]}) skipped=${!SKIP[*]}" >> $L/log
sleep 240
git restore --source=HEAD --worktree -- src test/unit CMakeLists.txt cmake
git status --short src test cmake CMakeLists.txt | head -3 >> $L/log
touch $L/DONE2
