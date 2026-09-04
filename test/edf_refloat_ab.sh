#!/usr/bin/env bash
# A/B for the two refloat levers introduced 2026-09-04, on EldraziDisplacerFlicker.
#
#   MTG_REFLOAT_WILD_C  a rainbow source's wild float also credits wild_c when its produces
#                       include {C}  (Aether Hub -> can pay Displacer's {2}{C} / Depleter's {1}{C})
#   MTG_REFLOAT_NEED    the tap-ahead's colour commit scores {C} and BATTLEFIELD ability costs as
#                       demand, decrements demand as it commits (coverage), and stops excluding
#                       rainbow sources
#
# THREE ARMS IN ONE POOLED BATCH. Both levers are process-wide statics reached through a heurarm
# slot, which is the whole reason all three arms can ride a single work queue instead of three
# invocations -- one load-imbalance tail, not three (repo rule: no waves, no per-arm splits).
#
# PAIRED BY CONSTRUCTION: every arm runs the same seeds and the same game indices, so arm-vs-arm is
# a paired comparison over identical shuffles and the per-game differences can be averaged directly.
#
# CHUNKED 25 GAMES PER JOB, deliberately. The previous EDF measurement used 8 coarse 100-game jobs
# and drained to 5 of 24 workers near the end (one game ran 0.26 h); finer jobs keep the pool full
# against the same tail. A chunk sets seed = base + game_index and carries game_index, so the games
# are the identical sequence a single 100-game job would run.
set -uo pipefail
cd "$(dirname "$0")/.."

DECK=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod
PROF=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
OUT=logs/edf_refloat_ab
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
SEEDS = [4101, 4102, 4103, 4104, 4105, 4106, 4107, 4108]
ARMS = {
    "base":  {"MTG_REFLOAT_WILD_C": False, "MTG_REFLOAT_NEED": False},
    "wildc": {"MTG_REFLOAT_WILD_C": True,  "MTG_REFLOAT_NEED": False},
    "both":  {"MTG_REFLOAT_WILD_C": True,  "MTG_REFLOAT_NEED": True},
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
# MTG_DUMP_WINS gives the PER-GAME win turn, which is what makes the comparison paired. The job
# averages on stdout cannot do that: they are already collapsed over the shuffles the pairing exists
# to cancel.
MTG_DUMP_WINS=1 build/Release/mtg --batch "$OUT/manifest.json" --threads 0 \
    >"$OUT/batch.out" 2>"$OUT/batch.err"
rc=$?
echo "batch rc=$rc"
grep -E "heartbeat|SLOW-GAME" "$OUT/batch.err" | tail -20
echo "--- results ---"
python3 test/edf_refloat_report.py "$OUT/batch.err"
