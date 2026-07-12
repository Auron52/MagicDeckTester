#!/bin/bash
# "Go from there": (1) HELD-OUT validation of the TOPM winners on FRESH seeds (not in the train/
# frontier/depth sets), and (2) PRIOR-CONCENTRATED COMPUTE -- the prior frees ~2x wall-time, so
# reinvest it into higher reshuffle-width K on the top-M and ask: does top-M @ K32 beat off @ K16 at
# equal-or-less cost? A yes = a strictly better quality-per-second frontier (the real deliverable
# beyond same-quality-faster). Clean CPU, one config at a time. Outputs -> logs/model_improve/.
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve
HS="1111 2222 3333 5555 6666 8888"   # fresh held-out seeds

echo "[1/4] antilife K16 held-out  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/Anti-Lifegain.cod --dyn logs/eval/antilife_teacherd2_dyn.json \
    --max-turns 10 --seeds $HS -K 16 --topm 0 4 2 > "$OUT/frontier_antilife_k16.out" 2>&1

echo "[2/4] antilife K32 reinvest  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/Anti-Lifegain.cod --dyn logs/eval/antilife_teacherd2_dyn.json \
    --max-turns 10 --seeds $HS -K 32 --topm 0 8 4 2 > "$OUT/frontier_antilife_k32.out" 2>&1

echo "[3/4] TH K16 held-out  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/treasure_hunt.txt --dyn logs/eval/TH_teacherd2_dyn.json \
    --max-turns 8 --seeds $HS -K 16 --topm 0 8 4 > "$OUT/frontier_TH_k16.out" 2>&1

echo "[4/4] TH K32 reinvest  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/treasure_hunt.txt --dyn logs/eval/TH_teacherd2_dyn.json \
    --max-turns 8 --seeds $HS -K 32 --topm 0 8 4 > "$OUT/frontier_TH_k32.out" 2>&1

echo "=== FRONTIER DONE  $(date +%H:%M) ==="
