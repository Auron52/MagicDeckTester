#!/bin/bash
# ============================================================================================
# MASTER all-Sunday keep-model chain.  LAUNCH MANUALLY (Saturday night):
#     nohup bash test/keepmodel_overnight_chain.sh > logs/keepmodel_overnight/chain.log 2>&1 &
# Runs unattended, ONE phase at a time (no CPU oversubscription). Each phase is a keepmodel_overnight.sh
# run: keep-model-only (NON-DESTRUCTIVE -- reads the committed profile, stages <deck>.keepmodel.profile
# .json; committed profiles are never overwritten), with A/B vs committed + per-game flip audit.
# Nothing is committed or adopted. Trim by commenting out phases.
#
# Rough times at depth 5: ~1.5 h per eps at 12k hands, ~3.2 h per eps at 25k hands, + ~25 min A/B/run.
# Plan total ~20-22 h (fine for an all-day-Sunday-away window). Each phase writes its own dir, so an
# interrupted chain keeps every completed phase.
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."
mkdir -p logs/keepmodel_overnight
[ -x ./build/Release/mtg ] && [ -x ./build/Release/mtg-analyze ] || { echo "build Release first"; exit 1; }
phase(){ echo; echo "===== [$(date -u +%FT%TZ)] PHASE: $* ====="; echo; }

# --- Slivers: the focus. 4h vs 8h back-to-back = the sample-size diminishing-returns check. ---
phase "Slivers 4h  (12k hands, eps 0.02 & 0.05)"
KM_DECK=decks/slivers_vial.txt KM_HANDS=12000 KM_EPS="0.02 0.05" bash test/keepmodel_overnight.sh

phase "Slivers 8h  (25k hands, eps 0.02 & 0.05) -- compare REPORT to the 12k run"
KM_DECK=decks/slivers_vial.txt KM_HANDS=25000 KM_EPS="0.02 0.05" bash test/keepmodel_overnight.sh

# --- Other already-analyzed decks: first look at whether the new keep model helps them too. ---
# (12k hands, eps 0.02. Comment any line to skip. Burn is fast; th/antilife/knights vary.)
for d in decks/test_deck.txt decks/treasure_hunt.txt "decks/Anti-Lifegain.cod" decks/Knights.cod; do
  phase "Other deck: $d  (12k hands, eps 0.02)"
  KM_DECK="$d" KM_HANDS=12000 KM_EPS="0.02" bash test/keepmodel_overnight.sh
done

# --- Hinata LAST: slow combo deck (~10x slivers' per-hand cost). Probe showed eps=0.02 descends to
# hand size 2 -- the bottom-4/5 COMBO rollouts that dominate Hinata's cost AND are reached <3% of the
# time. So use eps=0.05 to skip them (force-keep floor at size 4): a big, cheap speedup for Hinata.
# Its "committed" input is the staged static d5 profile copied to decks/Hinata2.profile.json (no
# committed profile exists yet) -- the A/B is keep-model vs that static (the "default" to beat).
# Tune HANDS to the window's TAIL; KM_DEPTH=3 is a further ~2-3x speedup if needed (keep ~depth-insensitive).
phase "Hinata (slow combo) -- 8000 hands, eps 0.05  [tune to remaining window]"
KM_DECK=decks/Hinata2.cod KM_HANDS=8000 KM_EPS="0.05" bash test/keepmodel_overnight.sh

phase "ALL DONE -- review each logs/keepmodel_overnight/<deck>/<config>/REPORT.txt"
