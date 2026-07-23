#!/bin/bash
# ============================================================================================
# IN-SAMPLE keep-model A/B: replay the keep-model's OWN TRAINING games and compare the staged
# keep-model profile against the committed (static-mulligan) profile on TURN-REGRET.
#
# WHY: the policy-simulated baseline ([[keepmodel-policy-baseline-built]]) fixes the optimizer's
# curse, whose decisive symptom was the keep-model losing on its OWN training seeds (in-sample) --
# a model that loses in-sample is mislabeled, not undertrained. So the in-sample A/B is the direct
# test that the fix worked: the policy-baseline keep-model should now be >= committed in-sample.
#
# The trainer samples games at seed_off = analyzer_seed + 7,000,000, game g seeded seed_off+g. The
# runtime seeds game gi as (--seed)+gi, so --seed seed_off replays exactly those training games.
#
# OBJECTIVE = TURN-REGRET (the user's metric): per game, win_turn if won else max_turns+1 (a loss
# counts as one turn worse than the slowest win). LOWER is better. won/avg-win-turn are secondary.
#
# Usage:
#   KM=<staged km profile> [DECK=...] [SEED=...] [GAMES=...] [DEPTHS="0 3"] bash test/keepmodel_insample_ab.sh
#   (SEED defaults to the standard analyzer seed + 7e6 = 27260627.)
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

DECK=${DECK:-decks/Anti-Lifegain.cod}
STEM=$(basename "$DECK"); STEM=${STEM%.*}
COMMITTED=${COMMITTED:-decks/$STEM.profile.json}
KM=${KM:?set KM=<staged keep-model profile json>}
SEED=${SEED:-27260627}                  # analyzer seed 20260627 + 7,000,000 = training base
GAMES=${GAMES:-4000}                     # subset of the (typically 12000) training games
MAXT=${MAXT:-8}
IFS=' ' read -r -a DEPTHS <<< "${DEPTHS:-0 3}"
BIN=./build/Release/mtg
OUT=${OUT:-logs/keepmodel_overnight/$STEM/insample}
mkdir -p "$OUT"
LOSS=$((MAXT + 1))

[ -x "$BIN" ] || { echo "build Release first"; exit 1; }
[ -f "$KM" ] || { echo "no km profile: $KM"; exit 1; }

echo "=== IN-SAMPLE A/B  deck=$STEM  seed=$SEED  games=$GAMES  loss=$LOSS  ($(date -u +%H:%M:%SZ)) ==="
echo "    committed=$COMMITTED"
echo "    keepmodel=$KM"

arm(){ # $1=label $2=profile $3=depth -> dumps wins, prints turn-regret/won/avg
  local label=$1 prof=$2 d=$3
  MTG_DUMP_WINS=1 "$BIN" "$DECK" --profile "$prof" --seed "$SEED" --games "$GAMES" \
    --depth "$d" --budget-ms 20 --max-turns "$MAXT" --lookahead-bottoming --threads 0 \
    > "$OUT/agg_${label}_d${d}.txt" 2> "$OUT/dump_${label}_d${d}.err"
  grep -oE 'gi=[0-9]+ wt=-?[0-9]+' "$OUT/dump_${label}_d${d}.err" \
    | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1 \2/' | sort -n > "$OUT/wins_${label}_d${d}.wins"
}

for d in "${DEPTHS[@]}"; do
  arm committed "$COMMITTED" "$d"
  arm keepmodel "$KM" "$d"
  python3 - "$OUT/wins_committed_d${d}.wins" "$OUT/wins_keepmodel_d${d}.wins" "$d" "$LOSS" <<'PY'
import sys
cf, kf, d, loss = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
def load(fn):
    m={}
    for ln in open(fn):
        a=ln.split()
        if len(a)>=2: m[int(a[0])]=int(a[1])
    return m
c=load(cf); k=load(kf)
gis=sorted(set(c)&set(k))
def reg(m): return sum((m[g] if m[g]>0 else loss) for g in gis)/len(gis)
def won(m): return sum(1 for g in gis if m[g]>0)
def avg(m):
    w=[m[g] for g in gis if m[g]>0]; return sum(w)/len(w) if w else 0
# Loss-penalized: score a loss (v<=0) worse than any win, so a game becoming unwon folds into "slower"
# (tracked in slg = the maximal slowdowns) and an unwon game winning into "faster". No special category.
def _sc(v): return v if v>0 else 10000
fa=sl=0; slg=[]
for g in gis:
    cv,kv=c[g],k[g]
    if _sc(cv)==_sc(kv): continue
    if _sc(kv)<_sc(cv): fa+=1
    else:
        sl+=1
        if kv<=0: slg.append(g)
cr,kr=reg(c),reg(k)
print(f"  d{d} n={len(gis)}: TURN-REGRET committed={cr:.4f} keepmodel={kr:.4f} delta={kr-cr:+.4f} (neg=better)")
print(f"       won committed={won(c)} keepmodel={won(k)} | avg-win-turn {avg(c):.3f} -> {avg(k):.3f}")
print(f"       vs committed (loss-penalized score): faster={fa} slower={sl}{(' ->loss:'+str(slg[:10])) if slg else ''}")
PY
done
echo "=== DONE ($(date -u +%H:%M:%SZ)) ==="
