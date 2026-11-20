#!/bin/bash
# Manual bisect of the Five Colour d0 T1-fetch-hold change (gi67/gi192 s4004: old T5, new T7) over the
# src-touching commits 1443eeba0..c86a86333. Checks out ONLY src/ at each step (branch untouched), waits out
# the 9p clock lag before each checkout, builds Release, plays the 6 traced d0 games.
set -u
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
L=$A/bisect
mkdir -p $L
cd $N
mapfile -t C < <(git rev-list --reverse 1443eeba0..c86a86333 -- src)
lo=-1; hi=$(( ${#C[@]} - 1 ))   # C[lo] good (lo=-1 is 1443eeba0), C[hi] bad
echo "commits=${#C[@]}" > $L/log
test_commit() {   # $1 = commit; echo good|bad
  local c=$1
  sleep 240
  git restore --source="$c" --worktree -- src test/unit CMakeLists.txt cmake
  ./build.sh release -- --target mtg > $L/build_$c.log 2>&1 || { echo "BUILDFAIL"; return; }
  rm -rf $L/w_$c
  MTG_PGO=0 MTG_RSS_CAP_GB=6 build/Release/mtg --batch $A/fc_tr.json --threads 6 --game-log-dir $L/w_$c > /dev/null 2>&1
  local a b
  a=$(awk '{print $2}' $L/w_$c/fivecolour_overnight_d0_s4004_gi67_tr.wins)
  b=$(awk '{print $2}' $L/w_$c/fivecolour_overnight_d0_s4004_gi192_tr.wins)
  echo "$c gi67=$a gi192=$b" >> $L/log
  if [ "$a" = 5 ] && [ "$b" = 5 ]; then echo good; else echo bad; fi
}
while [ $(( hi - lo )) -gt 1 ]; do
  mid=$(( (lo + hi) / 2 ))
  r=$(test_commit ${C[$mid]})
  echo "step lo=$lo hi=$hi mid=$mid ${C[$mid]} -> $r" >> $L/log
  if [ "$r" = good ]; then lo=$mid; elif [ "$r" = bad ]; then hi=$mid; else echo "build failure at ${C[$mid]}; stopping" >> $L/log; break; fi
done
echo "FIRST BAD: ${C[$hi]}" >> $L/log
sleep 240
git restore --source=HEAD --worktree -- src test/unit CMakeLists.txt cmake
git status --short src test cmake CMakeLists.txt | head -3 >> $L/log
touch $L/DONE
