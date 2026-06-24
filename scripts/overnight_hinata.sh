#!/usr/bin/env bash
# overnight_hinata.sh -- unattended overnight runner for the Hinata2 analysis deliverables.
#
# Runs SEQUENTIALLY (no CPU oversubscription; the analyzer/miner each saturate the box):
#   1. Hinata2 profile (Stage 4)        -- the key deliverable; slow (combo deck, wide hands at d5).
#   2. Mulligan-profile QA on the 5 existing analyzed decks (regenerate + diff vs committed).
#   3. Hinata2 heuristic mining (BOUNDED) -- needs the profile from step 1; grounds prune candidates.
#
# Each phase is timestamped and logged under logs/overnight/. Safe to leave running overnight;
# the analyzer is budget-bounded (cannot hang) and the miner is GAMES-bounded.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$HERE"
OUT="$HERE/logs/overnight"; mkdir -p "$OUT"
log(){ echo "[$(date '+%H:%M:%S')] $*" | tee -a "$OUT/runner.log"; }

log "=== overnight_hinata START ==="

# --- Phase 1: Hinata2 profile (Stage 4) ---
log "Phase 1: Hinata2 profile (analyze_deck, no-rebuild) -> $OUT/01_profile.log"
python3 scripts/analyze_deck.py decks/Hinata2.cod --no-rebuild > "$OUT/01_profile.log" 2>&1
log "Phase 1 done rc=$? ; profile json: $(ls -la decks/Hinata2.profile.json 2>/dev/null || echo MISSING)"

# --- Phase 2: Mulligan-profile QA on existing decks ---
log "Phase 2: mulligan QA (existing decks) -> $OUT/02_qa.log"
bash scripts/mulligan_qa.sh > "$OUT/02_qa.log" 2>&1
log "Phase 2 done rc=$?"

# --- Phase 3: Hinata2 heuristic mining (bounded; needs the Phase-1 profile) ---
if [ -f decks/Hinata2.profile.json ]; then
  log "Phase 3: mine heuristics (GAMES=120, SEEDS '2002 3003') -> $OUT/03_mine.log"
  GAMES=120 SEEDS="2002 3003" bash scripts/mine_heuristics.sh decks/Hinata2.cod > "$OUT/03_mine.log" 2>&1
  log "Phase 3 done rc=$?"
else
  log "Phase 3 SKIPPED -- no Hinata2.profile.json (Phase 1 did not finish)."
fi

log "=== overnight_hinata COMPLETE ==="
