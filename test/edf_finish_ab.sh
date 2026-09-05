#!/usr/bin/env bash
# A/B for the KILL CHAIN introduced 2026-09-05, on EldraziDisplacerFlicker.
#
#   MTG_EDF_COMBO_FINISH  the loop's post-pass DEPLOYS the finisher: a drain/exile creature held in
#                         hand, or one a Living Wish can still fetch from the sideboard. Also what
#                         lets the go-off recognizer SEE that route (ScanHandSinks), so the loop is
#                         sized to fund the wish + the cast + the activations.
#   MTG_EDF_LOOP_DRAW     each iteration activates a {T} DRAW land (Mariposa's draw, the Investigate
#                         lands' Clue), so a loop with no finisher anywhere can go LOOKING for one.
#                         Gated on none being reachable yet -- it is a search, not a payoff.
#
# The two are separable and are measured separately because their costs differ by an order of
# magnitude: the finish half is a handful of scans per loop, the draw half puts real activations
# inside every rollout. `both` is the shipped candidate.
#
# ARMS IN ONE POOLED BATCH, chunked 25 games per job. Both levers ride heurarm slots for exactly this
# reason -- one work queue, one load-imbalance tail, no per-arm waves (CLAUDE.md).
#
# PAIRED BY CONSTRUCTION: every arm runs the same seeds and the same game indices, so the per-game
# differences cancel the shuffle.
set -uo pipefail
cd "$(dirname "$0")/.."

DECK=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod
PROF=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
OUT=logs/edf_finish_ab
mkdir -p "$OUT"

[[ -x build/Release/mtg ]] || { echo "build/Release/mtg missing -- run ./build.sh first" >&2; exit 1; }

GAMES=${GAMES:-100}
CHUNK=${CHUNK:-25}
DEPTH=${DEPTH:-5}
BUDGET=${BUDGET:-20}

python3 - "$DECK" "$PROF" "$OUT/manifest.json" "$GAMES" "$CHUNK" "$DEPTH" "$BUDGET" <<'PY'
import json, sys
deck, prof, out, games, chunk, depth, budget = sys.argv[1:8]
games, chunk, depth, budget = int(games), int(chunk), int(depth), int(budget)
# SEEDS MUST BE SPACED BY AT LEAST `games` -- see the long note in test/edf_aura_ab.sh. A game's
# SHUFFLE is `job.seed + local_index`, so consecutive bases 4201..4208 x 100 games covered
# [4201,4400] .. [4208,4407] = 107 distinct games replayed 7.5x, not 800 independent ones. The
# kill-chain result this script produced was FIRST REPORTED as "+0.2100 turns, t=9.18, 8/8 seeds";
# re-aggregated to the 107 distinct games it is "+0.2410 turns, t=3.83" -- the effect is real and
# slightly larger, but the significance was overstated by exactly sqrt(7.5), and "8/8 seeds" was
# never 8 independent replications. Spacing by 100 makes the blocks disjoint.
SEEDS = [4200, 4300, 4400, 4500, 4600, 4700, 4800, 4900]
ARMS = {
    # The behaviour shipped before the kill chain: cash only into a sink ALREADY on the battlefield.
    "base":   {"MTG_EDF_COMBO_FINISH": False, "MTG_EDF_LOOP_DRAW": False},
    "finish": {"MTG_EDF_COMBO_FINISH": True,  "MTG_EDF_LOOP_DRAW": False},
    "both":   {"MTG_EDF_COMBO_FINISH": True,  "MTG_EDF_LOOP_DRAW": True},
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
