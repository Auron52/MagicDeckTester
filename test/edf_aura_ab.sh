#!/usr/bin/env bash
# A/B for the LAND-AURA sequential admission (MTG_EDF_SEQ_AURA), on EldraziDisplacerFlicker.
#
#   MTG_EDF_SEQ_AURA  a land Aura CAST THIS TURN counts as same-turn mana at the odometer bound, the
#                     selection-exact gate, the sequential-walk admission and the walk itself, so a
#                     subset like {Wild Growth, Living Wish} -- payable only in sequence -- is
#                     enumerated instead of pruned before any gate sees it.
#
# THE QUESTION IS COST, NOT CORRECTNESS. The lines it unlocks are rules-legal and the user reported
# their absence as a bug; what is unmeasured is whether SEARCHING them pays, because this deck runs
# sixteen land Auras and admitting every {Aura, spell} pair costs ~2.9x wall clock on a 12-game
# probe (13.1 s -> 37.6 s) that also came out directionally WORSE (6.00 vs 5.75) on a sample far too
# small to mean anything. Shipped ON; this is what decides whether it stays on.
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
OUT=logs/edf_aura_ab
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
# SEEDS MUST BE SPACED BY AT LEAST `games`, because a game's SHUFFLE is `job.seed + local_index`
# (GoldFishRunner/BatchRunner) -- the seed is a BASE, not a stream id. Consecutive bases 4301..4308
# with 100 games each therefore cover [4301,4400] .. [4308,4407]: a union of 107 DISTINCT games, each
# replayed 7.5x, masquerading as 800 independent ones. Measured 2026-09-05, and it is not a rounding
# error: it inflated this A/B's t by sqrt(7.5) ~ 2.7x (the sibling edf_finish_ab.sh reported t=9.18
# for what is really t=3.83), and it burned ~4.7 h re-playing ONE pathological game (shuffle 4329)
# four times. Spacing by 100 makes the eight blocks disjoint, so "8 seeds" means 8 independent
# replicates and a per-seed win/loss tally is real evidence again.
# Kept disjoint from edf_finish_ab.sh's [4200,4999] as well, so the two A/Bs never share a game and
# either can serve as held-out seeds for the other.
SEEDS = [5000, 5100, 5200, 5300, 5400, 5500, 5600, 5700]
ARMS = {
    # MTG_EDF_SEQ_AURA: does admitting a same-turn land Aura to the odometer / the sequential
    # payability walk PAY, given it costs ~2.9x wall clock on this deck (sixteen land Auras, so
    # every {Aura, spell} pair widens the plan set)? Shipped ON as the user-requested behaviour and
    # explicitly unmeasured on quality; this is the measurement that settles the default.
    "base": {"MTG_EDF_SEQ_AURA": False},
    "aura": {"MTG_EDF_SEQ_AURA": True},
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
