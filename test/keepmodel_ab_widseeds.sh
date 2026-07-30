#!/usr/bin/env bash
# Wide-seed in-game A/B: a chosen keep-model form vs the committed static profile, over MANY
# seeds, to raise confidence past the 4-seed select run. Mirrors keepmodel_select.sh's A/B
# invocation EXACTLY (same engine, flags, budget) so results are comparable -- only the seed
# set is widened. Read-only w.r.t. deck profiles; all output under logs/keepmodel_ab_<stem>/.
#
#   KM_DECK=decks/slivers_vial.txt KM_FORM=hybrid bash test/keepmodel_ab_widseeds.sh
set -u
BIN=./build/Release/mtg
DECK=${KM_DECK:?set KM_DECK=decks/<name>.txt}
FORM=${KM_FORM:-hybrid}
CARDS=${KM_CARDS:-src/cards/data/cards.json}
STEM=$(basename "$DECK" .txt)
STATIC=decks/$STEM.profile.json
declare -A SRC=( [gini]="decks/$STEM.keepmodel.profile.json"
                 [regret]="decks/$STEM.keepmodel.regret.profile.json"
                 [score]="decks/$STEM.keepmodel.score.profile.json"
                 [hybrid]="decks/$STEM.keepmodel.hybrid.profile.json" )
CAND=${SRC[$FORM]:?unknown form $FORM}
# 16 distinct base seeds (4x the select run's 4). Includes the original 4 so the widened set
# is a strict superset -- the first four cells reproduce the select run as a determinism check.
SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}

OUT=logs/keepmodel_ab_$STEM; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== keep-model WIDE-SEED A/B ($(stamp)) deck=$STEM form=$FORM vs static ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=20 max-turns=8"

ab(){ local prof="$1" tag="$2"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_DUMP_WINS=1 "$BIN" "$DECK" --profile "$prof" --seed "$s" --games "$GAMES" \
      --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.wins" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

log "--- A/B static ($(stamp)) ---"; ab "$STATIC" static
log "--- A/B $FORM ($(stamp)) ---";  ab "$CAND"   "$FORM"

python3 - "$OUT" "$FORM" "$DEPTHS" "$SEEDS" <<'PY' | tee -a "$REPORT"
import sys,os
OUT=sys.argv[1]; form=sys.argv[2]; depths=[int(x) for x in sys.argv[3].split()]; seeds=[int(x) for x in sys.argv[4].split()]
def wins(tag,d,s):
    fn=f"{OUT}/wins_{tag}_d{d}_s{s}.wins"; w={}
    if not os.path.exists(fn): return w
    for ln in open(fn):
        if ln.startswith("[win]"):
            p=ln.split(); gi=int(p[1].split('=')[1]); wt=int(p[2].split('=')[1]); w[gi]=wt
    return w
def mean(xs): return sum(xs)/len(xs) if xs else 0.0
print(f"\n=== WIDE-SEED SELECTION ({form} vs static; negative delta = {form} beats static) ===")
print(f"{'depth':<6}{'static':>9}{form:>10}{'meanΔ':>10}{'W->L':>7}{'L->W':>7}{'seedsΔ<0':>10}")
grand_d=[]
for d in depths:
    dd=[]; wl=0; lw=0; sdeltas=[]
    for s in seeds:
        ws=wins("static",d,s); wk=wins(form,d,s)
        sv=[t for t in ws.values() if t>0]; kv=[t for t in wk.values() if t>0]
        sm=mean(sv); km=mean(kv); sdeltas.append(km-sm)
        for gi in ws:
            a=ws.get(gi,0); b=wk.get(gi,0)
            aw=a>0; bw=b>0
            # win-turn flip on the SAME game index: a earlier win / a win->loss etc.
            if aw and not bw: wl+=1
            elif bw and not aw: lw+=1
        dd.append((sm,km))
    sm=mean([x[0] for x in dd]); km=mean([x[1] for x in dd]); delta=km-sm
    nlt=sum(1 for x in sdeltas if x<0)
    grand_d.append(delta)
    print(f"d{d:<5}{sm:>9.3f}{km:>10.3f}{delta:>+10.4f}{wl:>7}{lw:>7}{str(nlt)+'/'+str(len(seeds)):>10}")
print(f"\noverall meanΔ (avg over depths): {mean(grand_d):>+.4f}  ({'BEATS static' if mean(grand_d)<0 else 'loses to static'})")
PY
log "=== WIDE-SEED A/B DONE ($(stamp)) ==="
