#!/bin/bash
# =============================================================================================
# Keep-model SELECTION by the real runner (Path B): the trainer is only a candidate GENERATOR;
# the SELECTOR/gate is an in-game A/B with ./build/Release/mtg -- NOT held-out regret (shown NOT
# to transfer to in-game win-turn; see docs/design/keep-model-label-noise-vs-transfer.md).
#
# One MTG_KEEP_SPLIT=both fit builds the (expensive) blind-averaged kv table ONCE and emits all
# candidate FORMS from it: gini-tree (primary .keepmodel.profile.json) + regret / score / hybrid
# side files. We then A/B every candidate AND the committed static with the real runner, rank by
# mean in-game win-turn, and report which -- if any -- beats static. NON-DESTRUCTIVE (committed
# profile read-only; all output under logs/keepmodel_select/).
#   KM_DECK=decks/slivers_vial/slivers_vial.txt KM_ROLLOUTS=8 KM_HANDS=4000 bash test/keepmodel_select.sh
# =============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg; AN=./build/Release/mtg-analyze
[ -x "$BIN" ] && [ -x "$AN" ] || { echo "build Release first"; exit 1; }

DECK=${KM_DECK:-decks/slivers_vial/slivers_vial.txt}
STEM=$(basename "$DECK"); STEM=${STEM%.*}
STATIC=decks/$STEM.profile.json
CARDS=src/cards/data/cards.json
HANDS=${KM_HANDS:-4000}; ROLLOUTS=${KM_ROLLOUTS:-8}; EPS=${KM_EPS:-0.02}; DEPTH=${KM_DEPTH:-5}
FITSEED=20260627
AB_SEEDS="4004 5005 6006 7007"; AB_DEPTHS="0 3 5"; AB_GAMES=${KM_AB_GAMES:-1000}
OUT=logs/keepmodel_select/$STEM; mkdir -p "$OUT"
REPORT="$OUT/REPORT.txt"; : > "$REPORT"
log(){ echo "$@" | tee -a "$REPORT"; }
stamp(){ date -u +%FT%TZ; }

# candidate form -> the file the analyzer writes it to (primary gini = .keepmodel.profile.json)
declare -A SRC=( [gini]="decks/$STEM.keepmodel.profile.json"
                 [regret]="decks/$STEM.keepmodel.regret.profile.json"
                 [score]="decks/$STEM.keepmodel.score.profile.json"
                 [hybrid]="decks/$STEM.keepmodel.hybrid.profile.json" )
FORMS="gini regret score hybrid"

log "=== keep-model SELECT-BY-RUNNER ($(stamp)) deck=$STEM ==="
log "one both-fit: hands=$HANDS rollouts=$ROLLOUTS eps=$EPS depth=$DEPTH ; A/B ${AB_GAMES}g seeds[$AB_SEEDS] depths[$AB_DEPTHS]"

# --- ONE fit (MTG_KEEP_SPLIT=both) emits every form from one kv table ---
if [ -f "$OUT/.fit_done" ]; then log "fit: SKIP (cached)"; else
  log "--- both-fit ($(stamp)) ---"; t0=$SECONDS
  MTG_KEEP_MODEL_ONLY=1 MTG_KEEP_SPLIT=both MTG_KEEP_GAMES=$HANDS MTG_KEEP_REACH_EPS=$EPS \
    MTG_KEEP_ROLLOUTS=$ROLLOUTS MTG_ANALYZE_DEPTH=$DEPTH MTG_ANALYZE_SCALE=2 \
    "$AN" "$DECK" --cards-json "$CARDS" --max-turns 8 --seed "$FITSEED" > "$OUT/fit_both.log" 2>&1
  rc=$?; log "  fit rc=$rc time=$((SECONDS-t0))s"
  [ $rc -eq 0 ] || { log "  FIT FAILED, see $OUT/fit_both.log"; exit 1; }
  for f in $FORMS; do [ -f "${SRC[$f]}" ] && cp "${SRC[$f]}" "$OUT/cand_$f.profile.json" && log "  emitted $f"; done
  touch "$OUT/.fit_done"
fi

ab(){ local prof=$1 tag=$2
  for d in $AB_DEPTHS; do for s in $AB_SEEDS; do
    MTG_DUMP_WINS=1 "$BIN" "$DECK" --profile "$prof" --seed "$s" --games "$AB_GAMES" \
      --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/agg_${tag}_d${d}_s${s}.txt" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
    grep -oE 'gi=[0-9]+ wt=-?[0-9]+' "$OUT/err_${tag}_d${d}_s${s}.txt" \
      | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1 \2/' | sort -n > "$OUT/wins_${tag}_d${d}_s${s}.wins"
  done; done
}
log ""; log "--- A/B static baseline ($(stamp)) ---"; ab "$STATIC" static
for f in $FORMS; do [ -f "$OUT/cand_$f.profile.json" ] || continue
  log "--- A/B $f ($(stamp)) ---"; ab "$OUT/cand_$f.profile.json" "$f"; done

log ""; log "=== SELECTION (mean in-game win-turn; negative delta = beats static) ==="
python3 - "$OUT" "$FORMS" "$AB_DEPTHS" "$AB_SEEDS" <<'PY' | tee -a "$REPORT"
import os,sys
OUT=sys.argv[1]; forms=sys.argv[2].split(); depths=[int(x) for x in sys.argv[3].split()]; seeds=[int(x) for x in sys.argv[4].split()]
def wins(fn):
    d={}
    if os.path.exists(fn):
        for ln in open(fn):
            a=ln.split()
            if len(a)>=2: d[int(a[0])]=int(a[1])
    return d
def score(tag):
    tot=0.0;n=0;wl=lw=0;per={}
    for d in depths:
        sa_s=ka_s=0.0;cnt=0
        for s in seeds:
            ws=wins(f"{OUT}/wins_static_d{d}_s{s}.wins"); wk=wins(f"{OUT}/wins_{tag}_d{d}_s{s}.wins")
            if not ws or not wk: continue
            sv=[t for t in ws.values() if t>0]; kv=[t for t in wk.values() if t>0]
            sa=sum(sv)/len(sv) if sv else 0; ka=sum(kv)/len(kv) if kv else 0
            sa_s+=sa;ka_s+=ka;cnt+=1;tot+=(ka-sa);n+=1
            for gi in ws:
                g=ws[gi];nn=wk.get(gi,g)
                if g>0 and nn<0: wl+=1
                elif g<0 and nn>0: lw+=1
        if cnt: per[d]=(sa_s/cnt,ka_s/cnt)
    return (tot/n if n else 0.0),per,wl,lw
print(f"{'form':<8}{'d0 s/c':>16}{'d3 s/c':>16}{'d5 s/c':>16}{'meanΔ':>10}  W->L/L->W")
best=None
for f in forms:
    if not os.path.exists(f"{OUT}/cand_{f}.profile.json"): continue
    md,per,wl,lw=score(f)
    def cell(d):
        a=per.get(d,(0,0)); return f"{a[0]:.3f}/{a[1]:.3f}"
    print(f"{f:<8}{cell(0):>16}{cell(3):>16}{cell(5):>16}{md:>+10.4f}  {wl}/{lw}")
    if best is None or md<best[1]: best=(f,md)
print()
if best and best[1]<-0.001: print(f"WINNER: '{best[0]}' beats static in-game by {-best[1]:.4f} turns -> adoption candidate")
elif best: print(f"NO FORM BEATS STATIC (best '{best[0]}' {best[1]:+.4f}) -> keep static (honest selector confirms)")
PY
log ""; log "=== SELECT DONE ($(stamp)) ==="
