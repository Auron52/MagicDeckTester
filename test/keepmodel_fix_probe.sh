#!/bin/bash
# Controlled, FIXED-gen-seed probe of the hybrid TH partition fix (tie-break-deeper + finer min_leaf).
# Unlike keepmodel_score_ab.sh (random gen seed), this pins the analyzer seed so a re-run is reproducible
# and the hybrid model is comparable across binaries. A/Bs committed/regret/score/hybrid at d3.
#   Usage: [GENSEED=12345 GEN_GAMES=6000 GAMES=600 SEEDS="4004 5005 6006 7007" DECKS="th slivers burn"] \
#          bash test/keepmodel_fix_probe.sh
set -uo pipefail
cd "$(dirname "$0")/.."
GENSEED=${GENSEED:-12345}; GEN_GAMES=${GEN_GAMES:-6000}; EPS=${EPS:-0.2}; ITERS=${ITERS:-3}
GAMES=${GAMES:-600}; SEEDS=${SEEDS:-"4004 5005 6006 7007"}; MAXT=${MAXT:-8}; DEPTHS=${DEPTHS:-"3"}
DECKS=${DECKS:-"th slivers burn"}
BIN=${BIN:-./build/Release/mtg}; ANALYZE=${ANALYZE:-./build/Release/mtg-analyze}
declare -A DECK_FILE=( [burn]=decks/test_deck.txt [antilife]=decks/Anti-Lifegain.cod
  [slivers]=decks/slivers_vial.txt [th]=decks/treasure_hunt.txt [hinata]=decks/Hinata2.cod)
declare -A DECK_PROF=( [burn]=decks/test_deck.profile.json [antilife]=decks/Anti-Lifegain.profile.json
  [slivers]=decks/slivers_vial.profile.json [th]=decks/treasure_hunt.profile.json [hinata]=decks/Hinata2.profile.json)
ROOT=logs/keepmodel_fix_probe; mkdir -p "$ROOT"
echo "=== fix probe  genseed=$GENSEED gen=${GEN_GAMES}h eps$EPS iters$ITERS | ab=${GAMES}g d=$DEPTHS seeds=$SEEDS ==="
run(){ local bud=20; [ "$3" = 0 ] && bud=0
  MTG_DUMP_WINS=1 "$BIN" "$5" --profile "$2" --seed "$4" --games "$GAMES" --depth "$3" \
    --budget-ms $bud --max-turns "$MAXT" --lookahead-bottoming --threads 0 2>&1 1>/dev/null \
    | grep -oE 'gi=[0-9]+ wt=-?[0-9]+' | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1 \2/' | sort -n > "$6/${1}_d${3}_s${4}.wins"; }
for key in $DECKS; do
  df=${DECK_FILE[$key]}; pf=${DECK_PROF[$key]}
  [ -f "$df" ] && [ -f "$pf" ] || { echo "[$key] missing -- skip"; continue; }
  work="$ROOT/$key"; mkdir -p "$work"; stem=$(basename "$df"); stem="${stem%.*}"
  cp "$df" "$work/"; cp "$pf" "$work/${stem}.profile.json"; wdeck="$work/$(basename "$df")"
  echo ""; echo "##### [$key] gen (seed=$GENSEED) #####"
  MTG_KEEP_MODEL_ONLY=1 MTG_KEEP_SPLIT=both MTG_KEEP_BASELINE=policy MTG_KEEP_POLICY_ITERS=$ITERS \
  MTG_KEEP_GAMES=$GEN_GAMES MTG_ANALYZE_DEPTH=3 MTG_KEEP_REACH_EPS=$EPS MTG_KEEP_ROLLOUTS=1 \
    "$ANALYZE" "$wdeck" --seed "$GENSEED" > "$work/gen.log" 2>&1
  grep -E "regret\).*chose depth|hybrid partition-depth [0-9].*leaves|final HYBRID" "$work/gen.log" | tail -8 | sed 's/^/    /'
  committed="$work/${stem}.profile.json"; gini="$work/${stem}.keepmodel.profile.json"
  regret="$work/${stem}.keepmodel.regret.profile.json"; score="$work/${stem}.keepmodel.score.profile.json"
  hybrid="$work/${stem}.keepmodel.hybrid.profile.json"
  [ -f "$gini" ] || { echo "  [$key] no model -- skip"; continue; }
  for d in $DEPTHS; do for s in $SEEDS; do
    run committed "$committed" "$d" "$s" "$wdeck" "$work"
    run regret "$regret" "$d" "$s" "$wdeck" "$work"
    [ -f "$score" ] && run score "$score" "$d" "$s" "$wdeck" "$work"
    run hybrid "$hybrid" "$d" "$s" "$wdeck" "$work"
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
arms=["committed","regret","score","hybrid"]
print(f"== [{key}] turn-regret (loss={loss}; lower=better) ==")
for d in depths:
    tot={a:0.0 for a in arms}; n=0; wl={a:0 for a in arms}; have={a:True for a in arms}
    for s in seeds:
        M={a:load(f"{work}/{a}_d{d}_s{s}.wins") for a in arms}
        for a in arms:
            if not M[a]: have[a]=False
        present=[a for a in arms if M[a]]
        if "committed" not in present: continue
        gis=sorted(set.intersection(*[set(M[a]) for a in present]))
        for a in present:
            tot[a]+=reg(M[a],gis)*len(gis)
            wl[a]+=sum(1 for x in gis if M["committed"][x]>0 and M[a][x]<0)
        n+=len(gis)
    if not n: continue
    c=tot["committed"]/n; line=f"  d{d} (n={n}): committed {c:.4f}"
    for a in arms[1:]:
        if have[a]: line+=f" | {a} {tot[a]/n:.4f} ({(tot[a]-tot['committed'])/n:+.4f}, W->L {wl[a]})"
    print(line)
PY
done
echo ""; echo "=== done. logs under $ROOT ==="
