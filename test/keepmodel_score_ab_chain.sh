#!/bin/bash
# Tiered driver for the 4-way score A/B (keepmodel_score_ab.sh): fast decks at high sample, slow decks
# (slivers/th/hinata combo/flood) at reduced sample so the whole sweep finishes in one overnight window.
# Runs sequentially (no CPU oversubscription -> deterministic d3 budget timing). Nothing committed.
set -uo pipefail
cd "$(dirname "$0")/.."
LOG=logs/keepmodel_score_ab/chain.log
mkdir -p logs/keepmodel_score_ab
echo "=== tiered score A/B chain start ===" | tee "$LOG"

# tier: DECKS GEN_GAMES GAMES
run_tier(){
  echo "" | tee -a "$LOG"; echo ">>> tier: DECKS='$1' GEN=$2 AB=$3" | tee -a "$LOG"
  DECKS="$1" GEN_GAMES="$2" GAMES="$3" ITERS=3 EPS=0.2 GEN_DEPTH=3 \
    SEEDS="4004 5005 6006 7007" DEPTHS="0 3" \
    bash test/keepmodel_score_ab.sh 2>&1 | tee -a "$LOG"
}

run_tier "burn antilife knights" 5000 800
run_tier "slivers th"            3000 600
run_tier "hinata"                2500 500

echo "" | tee -a "$LOG"; echo "=== CHAIN COMPLETE ===" | tee -a "$LOG"
