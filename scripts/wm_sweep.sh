#!/bin/bash
# World-model aux-weight sweep. PRIMARY task = rank rsvalue candidates (value head); AUX = dynamics/recon/
# bootstrap on teacher trajectories (shared encoder). Question: does ANY dynamics weighting push held-out
# pick-regret below the value-only baseline (~0.17) toward the K=8 label-noise floor (~0.12)? Reports
# static + BOOTSTRAPPED-value ranking for each (recon-w, boot-w). Each config is a self-contained chunk.
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve; WM=/tmp/worldmodel
g++ -O2 -std=c++17 -o "$WM" tools/worldmodel/main.cpp 2>/dev/null
run () {  # name rsvalue traj
  local NM="$1" RS="$2" TR="$3"
  echo "===== $NM  (traj $(awk 'NR>1{print $(NF-2)}' "$TR"|sort -u|wc -l) games)  $(date +%H:%M) ====="
  echo "  floor~0.12 (antilife) / 0.10 (TH);  value-only baseline below:"
  for cfg in "0 0" "0 0.5" "0.3 0.3" "1 1" "0.1 0.5" "0.5 0"; do
    set -- $cfg; rw=$1; bw=$2
    local line
    line=$("$WM" "$RS" --traj "$TR" --H 96 --epochs 80 --recon-w $rw --boot-w $bw 2>&1 | grep "^\[rank")
    local st=$(echo "$line" | grep "stat" | grep -oE "pick-regret=[0-9.]+" | cut -d= -f2)
    local bo=$(echo "$line" | grep "BOOT" | grep -oE "pick-regret=[0-9.]+" | cut -d= -f2)
    printf "  recon-w=%-4s boot-w=%-4s  regret: static=%s  bootstrapped=%s\n" "$rw" "$bw" "$st" "$bo"
  done
}
echo "START $(date +%H:%M)"
run antilife logs/eval/antilife_rsvalue_teacherd2.rows logs/model_improve/traj/antilife.rows
run TH       logs/eval/TH_rsvalue_teacherd2.rows       logs/model_improve/traj/TH.rows
echo "=== WM-SWEEP DONE $(date +%H:%M) ==="
