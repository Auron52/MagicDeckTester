#!/usr/bin/env bash
# Overnight dynamic-d0 sweep. After the do-nothing scale-bug fix, the dyn model plays at heuristic
# parity on TH (5.60 vs 5.643 LP) with a 0.85 LP gap to the NC teacher (4.787). This asks: can ANY
# training config / DAgger round push a Seam-A d0 spell-ranker PAST heuristic parity toward the teacher?
#
# Trains a grid of (rows, T, H, epochs) models, measures d0 PLAY LP for each, logs to a findings file.
# Then runs 2 DAgger rounds on the best base config. Bounded (~1-2h). Read the log in the morning.
set -u
cd /workspaces/MagicDeckTester2
LOG=logs/eval/overnight_dyn_findings.txt
DT=/tmp/dyntrain
MODELDIR=logs/eval/overnight
mkdir -p "$MODELDIR"
g++ -O2 -std=c++17 -o "$DT" tools/dyntrain/main.cpp || { echo "trainer build failed" | tee -a "$LOG"; exit 1; }
echo "===================================================================" | tee -a "$LOG"
echo "OVERNIGHT DYN SWEEP  start=$(date '+%F %T')" | tee -a "$LOG"
echo "===================================================================" | tee -a "$LOG"

# measure_lp <deck> <mt> <model.json> <games> "<seeds>"  -> prints "win% LP"
measure_lp() {
  local deck="$1" mt="$2" model="$3" games="$4" seeds="$5"
  python3 - "$deck" "$mt" "$model" "$games" "$seeds" <<'PY'
import os,re,subprocess,sys
deck,mt,model,games=sys.argv[1],int(sys.argv[2]),sys.argv[3],int(sys.argv[4])
seeds=[int(x) for x in sys.argv[5].split()]
env={k:v for k,v in os.environ.items() if not k.startswith("MTG_")}
if model!="HEUR": env["MTG_DYN_MODEL"]=model
sw=sp=0; lps=[]
for s in seeds:
    out=subprocess.run(["build/Release/mtg",deck,"--games",str(games),"--seed",str(s),
        "--depth","0","--max-turns",str(mt),"--threads","6"],capture_output=True,text=True,env=env).stdout
    p=int(re.search(r"Games played\s*:\s*(\d+)",out).group(1)); w=int(re.search(r"Games won\s*:\s*(\d+)",out).group(1))
    m=re.search(r"Avg win turn\s*:\s*([\d.]+)",out); a=float(m.group(1)) if m else 0.0
    lps.append((w*a+(p-w)*(mt+1))/p); sw+=w; sp+=p
print("%.1f %.3f"%(100*sw/sp,sum(lps)/len(lps)))
PY
}

DECK=decks/treasure_hunt.txt; MT=8
BASE=logs/eval/TH_ncteach_eval.rows          # clean teacher rows
POOL=logs/eval/TH_dagger_combined.rows       # teacher + round-1 on-policy

echo "--- reference (heuristic) ---" | tee -a "$LOG"
printf "  %-34s %s\n" "heuristic d0" "$(measure_lp $DECK $MT HEUR 100 "2002 3003 7007")" | tee -a "$LOG"

echo "--- PHASE 1: config grid (train->measure play LP @60g x2seed) ---" | tee -a "$LOG"
best_lp=999; best_model=""; best_desc=""
for rows_desc in "base:$BASE" "pool:$POOL"; do
  rlbl="${rows_desc%%:*}"; rows="${rows_desc##*:}"
  for T in 0 2 3; do
    for H in 64 128; do
      ep=100
      mdl="$MODELDIR/TH_${rlbl}_T${T}_H${H}.json"
      tr=$("$DT" "$rows" --T $T --H $H --epochs $ep --lr 2e-3 --out "$mdl" 2>&1)
      reg=$(echo "$tr" | grep -oP '\[test \] .*pick-regret=\K[\d.]+' | head -1)
      res=$(measure_lp $DECK $MT "$mdl" 60 "2002 3003")
      lp=$(echo "$res" | awk '{print $2}')
      printf "  %-34s play(%s)  test-regret=%s\n" "${rlbl} T${T} H${H} ep${ep}" "$res" "${reg:-NA}" | tee -a "$LOG"
      awk "BEGIN{exit !($lp < $best_lp)}" && { best_lp=$lp; best_model="$mdl"; best_desc="${rlbl} T${T} H${H}"; }
    done
  done
done
echo "  >> PHASE1 BEST: $best_desc  (LP=$best_lp)  model=$best_model" | tee -a "$LOG"
echo "  >> re-measure best @100g x3seed:" | tee -a "$LOG"
printf "  %-34s %s\n" "$best_desc" "$(measure_lp $DECK $MT "$best_model" 100 "2002 3003 7007")" | tee -a "$LOG"

echo "--- PHASE 2: DAgger rounds on best config ---" | tee -a "$LOG"
# best config's T/H:
BT=$(echo "$best_desc" | grep -oP 'T\K\d+'); BH=$(echo "$best_desc" | grep -oP 'H\K\d+')
CURPOOL=/tmp/dyn_dagger_pool.rows
cp "$BASE" "$CURPOOL"           # start DAgger from the clean teacher pool + accumulate on-policy
CURMODEL="$best_model"
for round in 1 2; do
  echo "  [DAgger round $round] dumping on-policy states with current model ($BT/$BH)..." | tee -a "$LOG"
  DUMP=/tmp/dyn_dagger_r${round}.rows
  # on-policy: dyn model drives play; dump teacher (honest reshuffle-avg, K8 d2) labels per candidate
  MTG_DYN_MODEL="$CURMODEL" MTG_DUMP_EVAL_ROWS="$DUMP" MTG_EVAL_ROWS_K=8 \
    MTG_EVAL_ROWS_ROLLOUT=1 MTG_EVAL_ROWS_HONEST=1 MTG_EVAL_ROLLOUT_DEPTH=2 \
    build/Release/mtg $DECK --games 30 --seed $((5000+round)) --depth 0 --max-turns $MT --threads 6 >/dev/null 2>&1
  nnew=$(($(wc -l < "$DUMP")-1))
  # append new rows (skip header) to the pool
  tail -n +2 "$DUMP" >> "$CURPOOL"
  npool=$(($(wc -l < "$CURPOOL")-1))
  echo "    dumped $nnew on-policy rows; pool now $npool rows" | tee -a "$LOG"
  NEWMODEL="$MODELDIR/TH_dagger_r${round}.json"
  "$DT" "$CURPOOL" --T $BT --H $BH --epochs 100 --lr 2e-3 --out "$NEWMODEL" 2>/dev/null
  res=$(measure_lp $DECK $MT "$NEWMODEL" 100 "2002 3003 7007")
  printf "  %-34s %s\n" "DAgger r${round} (T${BT} H${BH})" "$res" | tee -a "$LOG"
  CURMODEL="$NEWMODEL"
done
echo "OVERNIGHT DYN SWEEP  done=$(date '+%F %T')" | tee -a "$LOG"
