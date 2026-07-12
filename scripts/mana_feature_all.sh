#!/bin/bash
# Confirm + generalize the color-aware castability win (MTG_MANA_FEATURES). TH showed a real standalone
# play gain (5.481->5.362) matching its 38% mana-aliasing; antilife (0% aliased) was flat. Hypothesis:
# the feature helps MULTI-COLOR decks (color actually screws) and is neutral on mono/0-aliased decks.
# Test all 5 decks at a bigger sample: dump resulting-state rows WITH the feature, train base vs mana
# from identical labels, compare held-out recall/regret + STANDALONE land-fold LP + PRIOR top-4 LP.
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve; DT=/tmp/dyntrain
g++ -O2 -std=c++17 -o "$DT" tools/dyntrain/main.cpp 2>/dev/null
DUMPSEEDS=$(seq 40001 40030); GAMES=6           # 180 games/deck
HELD="1111 2222 3333 5555 6666 8888 9099 1234"  # held-out play seeds

# land-fold play LP over held-out seeds. args: deck mt manaflag model topm(0=standalone via D0_LANDFOLD)
playlp () {
python3 - "$1" "$2" "$3" "$4" "$5" "$HELD" <<'PY'
import os,re,subprocess,sys
deck,mt,mf,model,topm,held=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4],sys.argv[5],sys.argv[6].split()
lps=[]
for s in held:
    env={k:v for k,v in os.environ.items() if not k.startswith("MTG_")}
    if topm=="0":  # standalone land-fold d0 (pure model, no NC rollout)
        env.update({"MTG_D0_LANDFOLD":"1","MTG_D0LF_K":"16","MTG_DYN_MODEL":model}); depth="0"
    else:          # NC prior top-M (model prunes, rollout decides)
        env.update({"MTG_NC_SEARCH":"1","MTG_NC_K":"16","MTG_NC_DEPTH":"2","MTG_NC_TOPM":topm,"MTG_DYN_MODEL":model}); depth="1"
    if mf=="1": env["MTG_MANA_FEATURES"]="1"
    out=subprocess.run(["build/Release/mtg",deck,"--games","40","--seed",s,"--depth",depth,
        "--max-turns",mt,"--threads","12"],capture_output=True,text=True,env=env).stdout
    try:
        p=int(re.search(r'played\s*:\s*(\d+)',out).group(1));w=int(re.search(r'won\s*:\s*(\d+)',out).group(1))
        a=float(re.search(r'Avg win turn\s*:\s*([\d.]+)',out).group(1));lps.append((w*a+(p-w)*(int(mt)+1))/p)
    except Exception: lps.append(int(mt)+1)
print("%.3f"%(sum(lps)/len(lps)))
PY
}

run_deck () {  # name deckfile mt tag
  local NM="$1" DECK="$2" MT="$3" TAG="$4"
  echo "===== $NM ($TAG)  $(date +%H:%M) ====="
  local MF="$OUT/mfa_${NM}.rows"; : > "$MF"
  for s in $DUMPSEEDS; do
    MTG_DUMP_RSVALUE_ROWS="$OUT/_ta_${NM}.rows" MTG_EVAL_ROWS_K=8 MTG_MANA_FEATURES=1 \
      MTG_EVAL_ROWS_ROLLOUT=1 MTG_EVAL_ROWS_HONEST=1 MTG_EVAL_ROLLOUT_DEPTH=2 \
      build/Release/mtg "$DECK" --games "$GAMES" --seed "$s" --depth 0 --max-turns "$MT" --threads 12 >/dev/null 2>&1
    if [ ! -s "$MF" ]; then cat "$OUT/_ta_${NM}.rows" >> "$MF"; else tail -n +2 "$OUT/_ta_${NM}.rows" >> "$MF"; fi
    rm -f "$OUT/_ta_${NM}.rows"
  done
  local BASE="$OUT/mfa_${NM}_base.rows"
  awk '{ out=$1; for(i=2;i<=NF-4;i++) out=out" "$i; out=out" "$(NF-1)" "$NF; print out }' "$MF" > "$BASE"
  echo "  rows=$(($(wc -l < "$MF")-1))"
  echo -n "  BASE: "; "$DT" "$BASE" --T 0 --H 128 --epochs 100 --lr 2e-3 --out "$OUT/mfa_${NM}_base_dyn.json" 2>&1 | grep "^\[test" | sed 's/pick-regret/reg/'
  echo -n "  MANA: "; "$DT" "$MF"   --T 0 --H 128 --epochs 100 --lr 2e-3 --out "$OUT/mfa_${NM}_mana_dyn.json" 2>&1 | grep "^\[test" | sed 's/pick-regret/reg/'
  local b_std m_std b_pri m_pri
  b_std=$(playlp "$DECK" "$MT" ""  "$OUT/mfa_${NM}_base_dyn.json" 0)
  m_std=$(playlp "$DECK" "$MT" "1" "$OUT/mfa_${NM}_mana_dyn.json" 0)
  b_pri=$(playlp "$DECK" "$MT" ""  "$OUT/mfa_${NM}_base_dyn.json" 4)
  m_pri=$(playlp "$DECK" "$MT" "1" "$OUT/mfa_${NM}_mana_dyn.json" 4)
  echo "  STANDALONE LP: base=$b_std  mana=$m_std   |   PRIOR top-4 LP: base=$b_pri  mana=$m_pri"
}

echo "START $(date +%H:%M)"
run_deck TH       decks/treasure_hunt.txt 8 "multi-color, 38% aliased -> expect HELP"
run_deck slivers  decks/slivers_vial.txt  8 "multi-color -> expect help"
run_deck knights  decks/Knights.cod       8 "multi-color -> expect help"
run_deck antilife decks/Anti-Lifegain.cod 10 "0% aliased control -> expect flat"
run_deck burn     decks/burn.txt          8 "mono-red control -> expect flat"
echo "=== MANA-FEATURE-ALL DONE $(date +%H:%M) ==="
