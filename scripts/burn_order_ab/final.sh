#!/bin/bash
# The follow-up's closing step, run UNDER the box lock (queue it with boxrun.sh batch bash final.sh) right
# behind the follow-up batch: (1) analyse new2, (2) ONE pooled batch with the HEAD binary -- recovery jobs
# for every new2 slower game not yet recovered + HEAD-vs-new2 identity chunks, (3) the burn reference
# bench and the strict protocol replay for the candidate arms. Everything written under build/scratch.
S=/workspaces/MagicDeckTester/build/scratch/burn-order
AB=$S/ab
WT=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
F=$AB/final
mkdir -p "$F"
LOG=$F/final.log
echo "start $(date -u +%FT%TZ)" > "$LOG"
n=0
while ! grep -q '^exit ' "$AB/new2/run.info" 2>/dev/null; do
  n=$((n + 1))
  if [ "$n" -gt 40 ]; then echo "new2 has not finished -- releasing the lock" >> "$LOG"; exit 1; fi
  sleep 3
done
if ! grep -q '^exit 0 ' "$AB/new2/run.info"; then echo "new2 failed: $(tail -1 "$AB/new2/run.info")" >> "$LOG"; exit 1; fi
python3 "$WT/scripts/burn_order_ab/final_prep.py" "$AB/base" "$AB/new2" "$F" >> "$LOG" 2>&1 || { echo "prep failed" >> "$LOG"; exit 1; }
env -C "$WT" MTG_PGO=0 MTG_DUMP_WINS=1 "$S/bin/mtg.head" --batch "$F/manifest.json" --threads 20 \
    > "$F/batch.out" 2> "$F/batch.err"
echo "batch exit $? $(date -u +%FT%TZ)" >> "$LOG"
python3 "$WT/scripts/burn_order_ab/final_check.py" "$AB/new2" "$F" >> "$LOG" 2>&1
# The reference bench (16 hand-played games, the shipped settings) per candidate arm, HEAD binary.
RB=$F/refbench
mkdir -p "$RB"
bench() {   # tag env...
  local tag=$1; shift
  mkdir -p "$RB/$tag"
  env -C "$WT" "$@" MTG_BIN="$S/bin/mtg.head" MTG_PGO=0 python3 scripts/ref_bench.py --deck burn --threads 16 \
      --log-root "$RB/$tag/trace" > "$RB/$tag/bench.out" 2>&1
  echo "refbench $tag rc=$?" >> "$LOG"
}
K="MTG_BURN_ORDER=1 MTG_BURN_M2_RULE=1"
bench off
bench o123ck $K MTG_SPECTACLE_PLAN_CREDIT=1 MTG_BURN_BLOOD_FIRST=1 MTG_KILL_TRIGGER_COMBO=1
bench o123k  $K MTG_SPECTACLE_PLAN_CREDIT=1 MTG_KILL_TRIGGER_COMBO=1
bench o12ck  $K MTG_BURN_BLOOD_FIRST=1 MTG_KILL_TRIGGER_COMBO=1
bench o13ck  MTG_BURN_ORDER=1 MTG_SPECTACLE_PLAN_CREDIT=1 MTG_BURN_BLOOD_FIRST=1 MTG_KILL_TRIGGER_COMBO=1
bench o1ck   MTG_BURN_ORDER=1 MTG_BURN_BLOOD_FIRST=1 MTG_KILL_TRIGGER_COMBO=1
# The strict protocol replay of every burn reference (the recorded lines must still replay).
proto() {   # tag env...
  local tag=$1; shift
  env -C "$WT" "$@" MTG_BIN="$S/bin/mtg.head" python3 test/viewer_protocol_check.py --strict --threads 16 \
      --only references/burn/ > "$F/proto_$tag.out" 2>&1
  echo "proto $tag rc=$?" >> "$LOG"
}
proto off
proto o123ck $K MTG_SPECTACLE_PLAN_CREDIT=1 MTG_BURN_BLOOD_FIRST=1 MTG_KILL_TRIGGER_COMBO=1
proto o123k  $K MTG_SPECTACLE_PLAN_CREDIT=1 MTG_KILL_TRIGGER_COMBO=1
echo "end $(date -u +%FT%TZ)" >> "$LOG"
