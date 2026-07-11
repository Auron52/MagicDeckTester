#!/usr/bin/env bash
# Paired NEW-vs-OLD burn exhaustive-KEEP A/B on the OVERNIGHT seed grid.
# Directly compares the sampling-fixed NEW keep profile against the biased OLD one
# (NOT vs static, NOT vs the stale pre-exhaustive overnight GT). Bottoming is held at
# clairvoyant lookahead on BOTH arms (MTG_EXHAUSTIVE_BOTTOM=0) so the ONLY difference is
# the mulligan KEEP decision. Mirrors keepmodel_exhaustive_ab.sh's engine/flags/budget.
set -u
BIN=./build/Release/mtg
DECK=decks/burn.txt
OLD=/tmp/burn_OLD.profile.json          # biased r100 (0-land keeps DRAW=676/PLAY=23)
NEW=/tmp/burn_NEW.profile.json          # sampling-fixed R40 (DRAW=98/PLAY=0)
[ -f "$OLD" ] || { echo "missing OLD $OLD"; exit 1; }
[ -f "$NEW" ] || { echo "missing NEW $NEW"; exit 1; }

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}

OUT=logs/burn_regen/new_vs_old_overnight; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== BURN NEW-vs-OLD keep A/B ($(stamp)) OVERNIGHT seeds ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=20 max-turns=8"
log "OLD=$OLD  NEW=$NEW  (bottoming=lookahead both arms)"

ab(){ local prof="$1" tag="$2"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_EXHAUSTIVE_PROFILE=none MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=0 "$BIN" "$DECK" --profile "$prof" --seed "$s" \
      --games "$GAMES" --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.wins" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

log "--- baseline=OLD ($(stamp)) ---"; ab "$OLD" old
log "--- candidate=NEW ($(stamp)) ---"; ab "$NEW" new

python3 - "$OUT" old new "$DEPTHS" "$SEEDS" <<'PY' | tee -a "$REPORT"
import sys,os
OUT,base,cand=sys.argv[1],sys.argv[2],sys.argv[3]
depths=[int(x) for x in sys.argv[4].split()]; seeds=[int(x) for x in sys.argv[5].split()]
def wins(tag,d,s):
    fn=f"{OUT}/err_{tag}_d{d}_s{s}.txt"; w={}
    if not os.path.exists(fn): return w
    for ln in open(fn):
        if ln.startswith("[win]"):
            p=ln.split(); gi=int(p[1].split('=')[1]); wt=int(p[2].split('=')[1]); w[gi]=wt
    return w
def mean(xs): return sum(xs)/len(xs) if xs else 0.0
print(f"\n=== BURN NEW vs OLD (negative delta = NEW wins earlier) ===")
print(f"{'depth':<6}{'OLD':>10}{'NEW':>10}{'meanD':>10}{'nOLD':>8}{'nNEW':>8}{'O->L':>7}{'L->N':>7}{'seedsD<0':>10}")
grand=[]
for d in depths:
    dd=[]; wl=0; lw=0; sdel=[]; nO=0; nN=0
    for s in seeds:
        wa=wins(base,d,s); wb=wins(cand,d,s)
        av=[t for t in wa.values() if t>0]; bv=[t for t in wb.values() if t>0]
        nO+=len(av); nN+=len(bv)
        am=mean(av); bm=mean(bv); sdel.append(bm-am)
        allgi=set(wa)|set(wb)
        for gi in allgi:
            a=wa.get(gi,0); b=wb.get(gi,0); aw=a>0; bw=b>0
            if aw and not bw: wl+=1
            elif bw and not aw: lw+=1
        dd.append((am,bm))
    am=mean([x[0] for x in dd]); bm=mean([x[1] for x in dd]); delta=bm-am
    nlt=sum(1 for x in sdel if x<0); grand.append(delta)
    print(f"d{d:<5}{am:>10.3f}{bm:>10.3f}{delta:>+10.4f}{nO:>8}{nN:>8}{wl:>7}{lw:>7}{str(nlt)+'/'+str(len(seeds)):>10}")
gm=mean(grand)
print(f"\noverall meanD (avg over depths): {gm:>+.4f}  ({'NEW BEATS OLD' if gm<0 else 'NEW loses to OLD'})")
print("O->L = games OLD won that NEW lost;  L->N = games NEW won that OLD lost (win-count shift).")
PY
log "=== BURN NEW-vs-OLD DONE ($(stamp)) ==="
