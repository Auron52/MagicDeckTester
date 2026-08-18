#!/usr/bin/env bash
# Overnight Mirrorwing campaign (2026-08-18, user-requested).
#
#   phase 1  generate the libonly arm's keep table            (~3.5 h, the long pole)
#   phase 2  pool all three arms into ONE apparatus, emit, derive policies
#   phase 3  ONE pooled batch: base / trick / libonly, 60k games each, FULL traces
#   phase 4  analyses -> logs/overnight/reports/
#
# Three arms on ONE seed means base pairs with BOTH comparisons from the same games, and
# trick-vs-libonly falls out for free. Everything is staged; nothing shipped is touched.
#
# If phase 1 fails, phases 2-4 still run with base+trick so the night is not lost.
set -u -o pipefail

ROOT=/workspaces/MagicDeckTester
cd "$ROOT"
O=logs/overnight
mkdir -p $O/gen $O/reports $O/traces $O/batch
LOG=$O/run.log
say() { echo "[$(date -u +%H:%M:%S)] $*" | tee -a "$LOG"; }

DECKDIR="logs/deckcmp/Mirrorwing Dragon"
POOLPROF="$DECKDIR/app_pool/Mirrorwing Dragon.profile.json"   # pooled card_scores, all arms
VALUE="decks/Mirrorwing Dragon/Mirrorwing Dragon.value.json"
EQUIV="$DECKDIR/pooltable/Mirrorwing Dragon.keepmodel.gencache.json"
BASE_RAW="decks/Mirrorwing Dragon/Mirrorwing Dragon.keepmodel.exhaustive.raw.json.gz"
TRICK_RAW="logs/mullgen_trick2/Mirrorwing Trick Suite.keepmodel.exhaustive.raw.json"

say "=== overnight start; HEAD=$(git rev-parse --short HEAD) src=$(git rev-parse HEAD:src)"

# ---------------------------------------------------------------- phase 1: generate libonly
say "phase 1: generating the libonly keep table (base with Twinflame x3 -> Luxurious Libation x3)"
cp -f "$DECKDIR/libonly/Mirrorwing Dragon.txt" "$O/gen/libonly.txt"
cp -f "$POOLPROF" "$O/gen/libonly.profile.json"
cp -f "$VALUE"    "$O/gen/libonly.value.json"
MTG_KEEP_ROLLOUTS=10 MTG_KEEP_ACCEPT_K=1 \
  ./build/Release/mtg-analyze "$O/gen/libonly.txt" \
  --cards-json src/cards/data/cards.json --gen-mulligan fast \
  > $O/gen/gen.log 2>&1
GEN_RC=$?
LIB_RAW="$O/gen/libonly.keepmodel.exhaustive.raw.json"
if [ $GEN_RC -ne 0 ] || [ ! -s "$LIB_RAW" ]; then
  say "phase 1 FAILED (rc=$GEN_RC) -- continuing with base+trick only; see $O/gen/gen.log"
  ARMS="base trick"
else
  say "phase 1 done: $(grep -a 'play digest' $O/gen/gen.log | head -1)"
  say "  $(grep -aE 'continuous size-7 DONE' $O/gen/gen.log | tail -1)"
  ARMS="base trick libonly"
fi

# ---------------------------------------------------------------- phase 2: pool + derive
say "phase 2: pooling arms [$ARMS] into one apparatus"
LIB_DIGEST=$(python3 -c "import json;print(json.load(open('$LIB_RAW'))['meta']['play_digest'])" 2>/dev/null || echo "")
IMPORT_ARGS=( --out $O/store.json.gz
  --arm "base=$BASE_RAW" --arm "trick=$TRICK_RAW"
  --current-digest base=09447aef3cbe3904 --current-digest trick=8d2b252c3d78dc3d
  --equiv "$EQUIV" --synth-r base=10 )
if [ "$ARMS" = "base trick libonly" ]; then
  IMPORT_ARGS+=( --arm "libonly=$LIB_RAW" --current-digest "libonly=$LIB_DIGEST" )
fi
python3 scripts/keepstore.py import "${IMPORT_ARGS[@]}" >> "$LOG" 2>&1 \
  || { say "phase 2 import FAILED"; exit 1; }

for arm in $ARMS; do
  python3 scripts/keepstore.py emit $O/store.json.gz --arm $arm --out /tmp/on_${arm}.raw.json.gz >> "$LOG" 2>&1 \
    || { say "emit $arm FAILED"; exit 1; }
done

deck_of() {  # arm -> decklist path
  case "$1" in
    base)    echo "$DECKDIR/base/Mirrorwing Dragon.txt" ;;
    trick)   echo "$DECKDIR/trick/Mirrorwing Dragon.txt" ;;
    libonly) echo "$DECKDIR/libonly/Mirrorwing Dragon.txt" ;;
  esac
}
for arm in $ARMS; do
  ( MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=/tmp/on_${arm}.raw.json.gz \
    MTG_MERGE_OUT_PROFILE=/tmp/on_${arm}.keepmodel.exhaustive.profile.json \
    MTG_MERGE_OUT_RAW=/tmp/on_${arm}.out.raw.json \
    ./build/Release/mtg-analyze "$(deck_of $arm)" --cards-json src/cards/data/cards.json \
    > $O/gen/merge_${arm}.log 2>&1 ) &
done
wait
for arm in $ARMS; do
  say "  $arm: $(grep -a 'merged policy' $O/gen/merge_${arm}.log | tail -1)"
  mkdir -p "$O/app_$arm"
  cp -f "$POOLPROF" "$O/app_$arm/Mirrorwing Dragon.profile.json"
  cp -f /tmp/on_${arm}.keepmodel.exhaustive.profile.json \
        "$O/app_$arm/Mirrorwing Dragon.keepmodel.exhaustive.profile.json"
done

# ---------------------------------------------------------------- phase 3: one pooled batch
say "phase 3: ONE pooled batch, 60,000 games per arm, full traces"
python3 - "$ARMS" <<'PY' > $O/batch/manifest.json
import json, sys, os
arms = sys.argv[1].split()
D = "logs/deckcmp/Mirrorwing Dragon"; O = "logs/overnight"
R = os.path.abspath(".")
jobs = []
for a in arms:
    jobs.append({
        "name": a,
        "deck": f"{R}/{D}/{a}/Mirrorwing Dragon.txt",
        "deck_numbering": f"{R}/{D}/{a}/numbering.json",
        "games": 60000, "seed": 980000, "max_turns": 8,
        "profile": f"{O}/app_{a}/Mirrorwing Dragon.profile.json",
        "value_profile": "decks/Mirrorwing Dragon/Mirrorwing Dragon.value.json",
        "value_model": False, "ladder_value_leaf": True,
    })
json.dump({"jobs": jobs}, sys.stdout, indent=1)
PY
MTG_DUMP_WINS=1 MTG_PROVIDER_DECK="$(readlink -f 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod')" \
  ./build/Release/mtg --batch "$O/batch/manifest.json" --threads 32 \
  --game-trace-dir $O/traces \
  > $O/batch/batch.out 2> $O/batch/batch.err
say "phase 3 done: $(grep -ac '^\[win\]' $O/batch/batch.err) win lines; traces $(ls $O/traces | wc -l)"

# ---------------------------------------------------------------- phase 4: analyses
say "phase 4: analyses"
python3 scripts/overnight_analyse.py > $O/reports/REPORT.md 2>> "$LOG" \
  && say "report -> $O/reports/REPORT.md" || say "analysis FAILED (see $LOG)"
say "=== overnight complete"
