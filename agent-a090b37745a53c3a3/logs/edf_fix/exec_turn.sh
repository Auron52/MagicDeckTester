#!/bin/bash
# Print the executor tap trace for one turn of a diverge.sh run. usage: exec_turn.sh <tag> <gi> <turn>
S=/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/div
for arm in A B; do
  echo "--- $arm"
  grep "\[exec-tap\] T$3 " "$S/$1_g$2_$arm/stderr.txt" | head -12 | cut -c1-200
done
