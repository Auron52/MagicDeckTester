#!/bin/bash
# Matched 4-way mulligan A/B: committed-static vs gini-tree vs regret-tree vs ADDITIVE-SCORE keep model.
# For each deck it (1) fits all three keep-model forms from ONE shared kv table (MTG_KEEP_SPLIT=both +
# the out_score side file), under the policy-simulated baseline, into a scratch dir (decks/ stays
# clean), then (2) A/Bs each profile by turn-regret (loss = max_turns+1) at the given play depths.
# Nothing is committed or accepted -- this is a read-only comparison for review.
#
# Usage: [GEN_GAMES=6000 EPS=0.2 GEN_DEPTH=3 ITERS=3 ROLLOUTS=1 \
#         GAMES=800 SEEDS="4004 5005 6006 7007" DEPTHS="0 3" DECKS="burn antilife ..."] \
#        bash test/keepmodel_score_ab.sh
set -uo pipefail
cd "$(dirname "$0")/.."

GEN_GAMES=${GEN_GAMES:-6000}; EPS=${EPS:-0.2}; GEN_DEPTH=${GEN_DEPTH:-3}; ITERS=${ITERS:-3}
ROLLOUTS=${ROLLOUTS:-1}
GAMES=${GAMES:-800}; SEEDS=${SEEDS:-"4004 5005 6006 7007"}; DEPTHS=${DEPTHS:-"0 3"}; MAXT=${MAXT:-8}
DECKS=${DECKS:-"burn antilife knights slivers th hinata"}
BIN=./build/Release/mtg; ANALYZE=./build/Release/mtg-analyze

declare -A DECK_FILE=(
  [burn]=decks/test_deck.txt   [antilife]=decks/Anti-Lifegain.cod [knights]=decks/Knights.cod
  [slivers]=decks/slivers_vial.txt [th]=decks/treasure_hunt.txt   [hinata]=decks/Hinata2.cod)
declare -A DECK_PROF=(
  [burn]=decks/test_deck.profile.json [antilife]=decks/Anti-Lifegain.profile.json
  [knights]=decks/Knights.profile.json [slivers]=decks/slivers_vial.profile.json
  [th]=decks/treasure_hunt.profile.json [hinata]=decks/Hinata2.profile.json)

ROOT=logs/keepmodel_score_ab
mkdir -p "$ROOT"
echo "=== keepmodel score A/B  gen=${GEN_GAMES}h d${GEN_DEPTH} eps${EPS} iters${ITERS} rollouts${ROLLOUTS} | ab=${GAMES}g depths=${DEPTHS} ==="

run(){ # $1=label $2=profile $3=depth $4=seed $5=deckfile $6=outdir
  local bud=20; [ "$3" = 0 ] && bud=0
  MTG_DUMP_WINS=1 "$BIN" "$5" --profile "$2" --seed "$4" --games "$GAMES" --depth "$3" \
    --budget-ms $bud --max-turns "$MAXT" --lookahead-bottoming --threads 0 2>&1 1>/dev/null \
    | grep -oE 'gi=[0-9]+ wt=-?[0-9]+' | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1 \2/' | sort -n > "$6/${1}_d${3}_s${4}.wins"
}

for key in $DECKS; do
  df=${DECK_FILE[$key]}; pf=${DECK_PROF[$key]}
  if [ ! -f "$df" ] || [ ! -f "$pf" ]; then echo "[$key] missing deck/profile -- skip"; continue; fi
  work="$ROOT/$key"; mkdir -p "$work"
  stem=$(basename "$df"); stem="${stem%.*}"
  cp "$df" "$work/"; cp "$pf" "$work/${stem}.profile.json"
  wdeck="$work/$(basename "$df")"

  echo ""; echo "##### [$key] generating gini+regret+score (policy) #####"
  MTG_KEEP_MODEL_ONLY=1 MTG_KEEP_SPLIT=both MTG_KEEP_BASELINE=policy MTG_KEEP_POLICY_ITERS=$ITERS \
  MTG_KEEP_GAMES=$GEN_GAMES MTG_ANALYZE_DEPTH=$GEN_DEPTH MTG_KEEP_REACH_EPS=$EPS MTG_KEEP_ROLLOUTS=$ROLLOUTS \
    "$ANALYZE" "$wdeck" > "$work/gen.log" 2>&1
  echo "  (gen log tail:)"; grep -E "final ADDITIVE|weights|final tree|chose depth|features used|too few|written" "$work/gen.log" | sed 's/^/    /' | tail -25

  committed="$work/${stem}.profile.json"
  gini="$work/${stem}.keepmodel.profile.json"
  regret="$work/${stem}.keepmodel.regret.profile.json"
  score="$work/${stem}.keepmodel.score.profile.json"
  hybrid="$work/${stem}.keepmodel.hybrid.profile.json"
  [ -f "$gini" ]   || { echo "  [$key] no gini model produced -- skip A/B"; continue; }

  echo "##### [$key] A/B #####"
  for d in $DEPTHS; do for s in $SEEDS; do
    run committed "$committed" "$d" "$s" "$wdeck" "$work"
    run gini      "$gini"      "$d" "$s" "$wdeck" "$work"
    [ -f "$regret" ] && run regret "$regret" "$d" "$s" "$wdeck" "$work"
    [ -f "$score" ]  && run score  "$score"  "$d" "$s" "$wdeck" "$work"
    [ -f "$hybrid" ] && run hybrid "$hybrid" "$d" "$s" "$wdeck" "$work"
  done; done

  python3 - "$work" "$((MAXT+1))" "$DEPTHS" "$SEEDS" "$key" <<'PY'
import sys,os
work,loss=sys.argv[1],int(sys.argv[2]); depths=sys.argv[3].split(); seeds=sys.argv[4].split(); key=sys.argv[5]
def load(fn):
    m={}
    if os.path.exists(fn):
        for ln in open(fn):
            a=ln.split()
            if len(a)>=2: m[int(a[0])]=int(a[1])
    return m
def reg(m,gis): return sum((m[g] if m[g]>0 else loss) for g in gis)/len(gis)
def won(m,gis): return sum(1 for g in gis if m[g]>0)
arms=["committed","gini","regret","score","hybrid"]
print(f"== [{key}] 4-way turn-regret (loss={loss}; lower=better) ==")
for d in depths:
    tot={a:0.0 for a in arms}; n=0; wl={a:0 for a in arms}
    have={a:True for a in arms}
    for s in seeds:
        M={a:load(f"{work}/{a}_d{d}_s{s}.wins") for a in arms}
        for a in arms:
            if not M[a]: have[a]=False
        present=[a for a in arms if M[a]]
        if "committed" not in present: continue
        gis=sorted(set.intersection(*[set(M[a]) for a in present]))
        if not gis: continue
        for a in present:
            tot[a]+=reg(M[a],gis)*len(gis)
            wl[a]+=sum(1 for x in gis if M["committed"][x]>0 and M[a][x]<0)
        n+=len(gis)
    if not n: continue
    c=tot["committed"]/n
    line=f"  d{d} ALL (n={n}): committed {c:.4f}"
    for a in arms[1:]:
        if have[a]: line+=f" | {a} {tot[a]/n:.4f} ({(tot[a]-tot['committed'])/n:+.4f}, W->L {wl[a]})"
    print(line)
PY
done
echo ""; echo "=== done. logs under $ROOT ==="
