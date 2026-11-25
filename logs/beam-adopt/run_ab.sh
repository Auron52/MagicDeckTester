#!/bin/bash
# Task 6: run both arms of the A/B back to back under ONE hold of the box lock (one big process at a time).
# usage: run_ab.sh <abdir>   (expects <abdir>/ab_B.json, ab_F.json, mtg.B, mtg.F)
set -u
D=$1
cd "$D" || exit 1
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash -c '
  for arm in B F; do
    mkdir -p wins_$arm
    env MTG_PGO=0 MTG_RSS_CAP_GB=40 ./mtg.$arm --batch ab_$arm.json --threads 0 --game-log-dir wins_$arm \
        > batch_$arm.out 2> batch_$arm.err
    echo "$arm rc=$?" >> rc.txt
  done'
echo done > DONE
