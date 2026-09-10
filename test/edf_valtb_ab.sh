#!/usr/bin/env bash
# A/B for the EDF PLAN-VALUE re-pricing (heuristic-optimization loop, 2026-09-10). Both levers are
# EldraziFlickerProvider::ComboCardValue and both default OFF, so `base` is the shipped engine.
#
#   MTG_EDF_VAL_RAMP   a land Aura is priced by the MANA it adds x the turns it will be tapped,
#                      instead of EvalCard's generic one-unit floor for a non-creature/burn/draw card.
#   MTG_EDF_VAL_COMBO  a creature is priced by its COMBO role (untapper = the mana it refunds; outlet
#                      or repeatable {C} sink = 3 mana-equivalents; anything else = the floor)
#                      instead of `power x expected-attacks` -- a COMBAT CLOCK this deck never uses.
#
# WHY BOTH HALVES, AND WHY 2x2. docs/design/edf-shortfall-classification.md item 3 says two of the
# deck's four +1 reference shortfalls (s10 T2, s11 T3) were decided at an EQUAL searched tail by
# `plan.value` alone, and both times it took the creature over the ramp. Neither half can flip that
# on its own -- no honest ramp price beats a 3/3's 1200 on turn 2, and no honest outlet price sinks
# below the ramp's 100 floor -- so the pair is the candidate and the singles are the attribution.
#
# NOTE THIS IS NOT ONLY A TIE-BREAK. EvalCard is also the d0/greedy leaf policy, so re-pricing cards
# changes the ROLLOUT that produces the tails, not just the root comparison at a tie. Expect upstream
# turns to move; that is the thing being measured.
#
# ARMS IN ONE POOLED BATCH, chunked 25 games per job -- both levers ride heurarm slots for exactly
# that reason: one work queue, one load-imbalance tail, no per-arm waves (CLAUDE.md).
#
# PAIRED BY CONSTRUCTION: every arm runs the same seeds and the same game indices, so the per-game
# differences cancel the shuffle. SEEDS ARE SPACED BY `games` (see test/edf_aura_ab.sh's note) so the
# eight blocks are disjoint rather than a 7.5x replay of one.
#
#   SEEDS="4200 4300 ..." bash test/edf_valtb_ab.sh     # train  (default)
#   SEEDS="$HELDOUT"      bash test/edf_valtb_ab.sh     # held-out confirmation
set -uo pipefail
cd "$(dirname "$0")/.."

DECK=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod
PROF=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
OUT=${OUT:-logs/edf_valtb_ab}
mkdir -p "$OUT"

[[ -x build/Release/mtg ]] || { echo "build/Release/mtg missing -- run ./build.sh first" >&2; exit 1; }

GAMES=${GAMES:-100}
CHUNK=${CHUNK:-25}
DEPTH=${DEPTH:-5}
BUDGET=${BUDGET:-20}
SEEDS=${SEEDS:-"4200 4300 4400 4500 4600 4700 4800 4900"}

python3 - "$DECK" "$PROF" "$OUT/manifest.json" "$GAMES" "$CHUNK" "$DEPTH" "$BUDGET" "$SEEDS" <<'PY'
import json, sys
deck, prof, out, games, chunk, depth, budget, seeds = sys.argv[1:9]
games, chunk, depth, budget = int(games), int(chunk), int(depth), int(budget)
SEEDS = [int(s) for s in seeds.split()]
ARMS = {
    "base":  {"MTG_EDF_VAL_RAMP": False, "MTG_EDF_VAL_COMBO": False},   # the shipped engine
    "ramp":  {"MTG_EDF_VAL_RAMP": True,  "MTG_EDF_VAL_COMBO": False},
    "combo": {"MTG_EDF_VAL_RAMP": False, "MTG_EDF_VAL_COMBO": True},
    "both":  {"MTG_EDF_VAL_RAMP": True,  "MTG_EDF_VAL_COMBO": True},
}
jobs = []
for arm, flags in ARMS.items():
    for s in SEEDS:
        for gi in range(0, games, chunk):
            n = min(chunk, games - gi)
            jobs.append({
                "name": f"edf_{arm}_s{s}_g{gi}",
                "deck": deck, "profile": prof,
                "games": n, "seed": s + gi, "game_index": gi,
                "depth": depth, "budget_ms": budget,
                "flags": flags, "weight": 0,
            })
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(f"{len(jobs)} jobs, {sum(j['games'] for j in jobs)} games "
      f"({len(ARMS)} arms x {len(SEEDS)} seeds x {games})")
PY

echo "--- running (d${DEPTH}/${BUDGET}ms, profile attached) ---"
MTG_DUMP_WINS=1 build/Release/mtg --batch "$OUT/manifest.json" --threads 0 \
    >"$OUT/batch.out" 2>"$OUT/batch.err"
rc=$?
echo "batch rc=$rc"
grep -E "heartbeat|SLOW-GAME" "$OUT/batch.err" | tail -20
echo "--- results ---"
python3 test/edf_refloat_report.py "$OUT/batch.err"
