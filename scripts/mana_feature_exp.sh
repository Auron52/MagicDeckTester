#!/bin/bash
# Test the color-aware castability feature (MTG_MANA_FEATURES) -- the concrete model route from the
# failure analysis (residual mis-ranks are mana-sequencing; HandCastableNow ignores color). Dump each
# deck's resulting-state rows WITH the feature, then train two models from the SAME labels: one on all
# columns, one with the 2 mana columns stripped (identical games/labels -> clean A/B isolating the
# feature). Compare held-out recall/regret + fail-analysis aliasing, then STANDALONE land-fold play LP.
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve; DT=/tmp/dyntrain
g++ -O2 -std=c++17 -o "$DT" tools/dyntrain/main.cpp 2>/dev/null
DUMPSEEDS=$(seq 40001 40020); GAMES=6
HELD="1111 2222 3333 5555 6666 8888 9099 1234"

# standalone land-fold play LP over held-out seeds. args: deck mt manaflag model
playlp () {
python3 - "$1" "$2" "$3" "$4" "$HELD" <<'PY'
import os,re,subprocess,sys
deck,mt,mf,model,held=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4],sys.argv[5].split()
lps=[]
for s in held:
    env={k:v for k,v in os.environ.items() if not k.startswith("MTG_")}
    env.update({"MTG_D0_LANDFOLD":"1","MTG_D0LF_K":"16","MTG_DYN_MODEL":model})
    if mf=="1": env["MTG_MANA_FEATURES"]="1"
    out=subprocess.run(["build/Release/mtg",deck,"--games","40","--seed",s,"--depth","0",
        "--max-turns",mt,"--threads","12"],capture_output=True,text=True,env=env).stdout
    try:
        p=int(re.search(r'played\s*:\s*(\d+)',out).group(1));w=int(re.search(r'won\s*:\s*(\d+)',out).group(1))
        a=float(re.search(r'Avg win turn\s*:\s*([\d.]+)',out).group(1));lps.append((w*a+(p-w)*(int(mt)+1))/p)
    except Exception: lps.append(int(mt)+1)
print("%.3f"%(sum(lps)/len(lps)))
PY
}

run_deck () {  # name deckfile mt
  local NM="$1" DECK="$2" MT="$3"
  echo "===== $NM  $(date +%H:%M) ====="
  local MF="$OUT/mf_${NM}.rows"; : > "$MF"
  for s in $DUMPSEEDS; do
    MTG_DUMP_RSVALUE_ROWS="$OUT/_t_${NM}.rows" MTG_EVAL_ROWS_K=8 MTG_MANA_FEATURES=1 \
      MTG_EVAL_ROWS_ROLLOUT=1 MTG_EVAL_ROWS_HONEST=1 MTG_EVAL_ROLLOUT_DEPTH=2 \
      build/Release/mtg "$DECK" --games "$GAMES" --seed "$s" --depth 0 --max-turns "$MT" --threads 12 >/dev/null 2>&1
    if [ ! -s "$MF" ]; then cat "$OUT/_t_${NM}.rows" >> "$MF"; else tail -n +2 "$OUT/_t_${NM}.rows" >> "$MF"; fi
    rm -f "$OUT/_t_${NM}.rows"
  done
  local BASE="$OUT/mf_${NM}_base.rows"
  awk '{ out=$1; for(i=2;i<=NF-4;i++) out=out" "$i; out=out" "$(NF-1)" "$NF; print out }' "$MF" > "$BASE"
  echo "  rows=$(($(wc -l < "$MF")-1))  mana_cols=$(head -1 "$MF" | wc -w)  base_cols=$(head -1 "$BASE" | wc -w)"
  echo "  --- BASE (no mana feat) ---"
  "$DT" "$BASE" --T 0 --H 96 --epochs 80 --lr 2e-3 --out "$OUT/mf_${NM}_base_dyn.json" 2>&1 | grep -E "^\[test|fail-analysis"
  echo "  --- MANA (color-aware castable) ---"
  "$DT" "$MF" --T 0 --H 96 --epochs 80 --lr 2e-3 --out "$OUT/mf_${NM}_mana_dyn.json" 2>&1 | grep -E "^\[test|fail-analysis"
  echo "  STANDALONE land-fold LP (held-out):  base=$(playlp "$DECK" "$MT" "" "$OUT/mf_${NM}_base_dyn.json")   mana=$(playlp "$DECK" "$MT" "1" "$OUT/mf_${NM}_mana_dyn.json")"
}

echo "START $(date +%H:%M)"
run_deck antilife decks/Anti-Lifegain.cod 10
run_deck TH       decks/treasure_hunt.txt 8
echo "=== MANA-FEATURE EXP DONE $(date +%H:%M) ==="
