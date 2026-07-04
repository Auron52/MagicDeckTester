#!/usr/bin/env bash
# BOTTOMING isolation for burn (=test_deck). KEEP is exhaustive on every arm; the only thing that varies
# is the BOTTOMING policy (which cards to put back after keeping an oversized hand at mull>=1):
#   A  uniform  keep + lookahead (clairvoyant) bottoming   MTG_EXHAUSTIVE_BOTTOM=0   -- the current default
#   B  uniform  keep + blind exhaustive R=100 bottoming    MTG_EXHAUSTIVE_BOTTOM=1
#   C  adaptive keep + blind exhaustive R=100 bottoming    MTG_EXHAUSTIVE_BOTTOM=1
# Comparisons:
#   B vs A : does R=100 blind exhaustive bottoming match/beat clairvoyant lookahead? (CLEAN: same keep
#            table, only bottoming differs.) R=20 lost d3/d5 by ~0.045t (majority R-noise) -- does R=100 close it?
#   C vs B : does the adaptive sampler's sub-cell-skipped bottom table (53.8% of cells differ from uniform
#            at the table level) cost anything IN PLAY? (Tiny 0.48% m0 keep confound; bottoming dominates.)
# Same engine flags as keepmodel_burn_3way.sh. Read-only w.r.t. profiles.
set -u
BIN=./build/Release/mtg
DECK=decks/test_deck.txt
UNIFORM=decks/test_deck.keepmodel.exhaustive.uniform.r100.profile.json
ADAPTIVE=decks/test_deck.keepmodel.exhaustive.adaptive.r100.profile.json
for f in "$UNIFORM" "$ADAPTIVE"; do [ -f "$f" ] || { echo "missing $f"; exit 1; }; done

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}

OUT=logs/keepmodel_exh_burn_bottom3; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== BURN 3-way BOTTOMING A/B ($(stamp)) ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=20 max-turns=8"

# ab <profile> <tag> <exhaustive_bottom 0|1>
ab(){ local prof="$1" tag="$2" exb="$3"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM="$exb" "$BIN" "$DECK" --profile "$prof" --seed "$s" \
      --games "$GAMES" --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.wins" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

log "--- A: uniform keep + lookahead bottoming ($(stamp)) ---";       ab "$UNIFORM"  A_lookahead 0
log "--- B: uniform keep + blind exhaustive bottoming ($(stamp)) ---"; ab "$UNIFORM"  B_unifblind 1
log "--- C: adaptive keep + blind exhaustive bottoming ($(stamp)) ---";ab "$ADAPTIVE" C_adapblind 1

python3 - "$OUT" "$DEPTHS" "$SEEDS" <<'PY' | tee -a "$REPORT"
import sys,os
OUT=sys.argv[1]; depths=[int(x) for x in sys.argv[2].split()]; seeds=[int(x) for x in sys.argv[3].split()]
def wins(tag,d,s):
    fn=f"{OUT}/err_{tag}_d{d}_s{s}.txt"; w={}
    if not os.path.exists(fn): return w
    for ln in open(fn):
        if ln.startswith("[win]"):
            p=ln.split(); w[int(p[1].split('=')[1])]=int(p[2].split('=')[1])
    return w
def mean(xs): return sum(xs)/len(xs) if xs else 0.0
def pair(base,cand,blabel,clabel):
    print(f"\n=== {clabel} vs {blabel} (negative delta = {clabel} wins earlier) ===")
    print(f"{'depth':<6}{'won_b':>10}{'won_c':>10}{'baseD':>9}{'candD':>9}{'meanD':>9}{'seedsD<0':>10}")
    grand=[]
    for d in depths:
        wb_tot=wc_tot=0; bms=[]; cms=[]; sdel=[]
        for s in seeds:
            wa=wins(base,d,s); wc=wins(cand,d,s)
            av=[t for t in wa.values() if t>0]; cv=[t for t in wc.values() if t>0]
            wb_tot+=len(av); wc_tot+=len(cv)
            am=mean(av); cm=mean(cv); bms.append(am); cms.append(cm); sdel.append(cm-am)
        am=mean(bms); cm=mean(cms); delta=cm-am; nlt=sum(1 for x in sdel if x<0)
        grand.append(delta)
        print(f"d{d:<5}{wb_tot:>10}{wc_tot:>10}{am:>9.3f}{cm:>9.3f}{delta:>+9.4f}{str(nlt)+'/'+str(len(seeds)):>10}")
    gm=mean(grand)
    print(f"overall meanD: {gm:>+.4f}  ({clabel+' BEATS '+blabel if gm<0 else clabel+' loses to '+blabel})")
pair("A_lookahead","B_unifblind","lookahead","uniform-blind")   # does R=100 blind match clairvoyant?
pair("B_unifblind","C_adapblind","uniform-blind","adaptive-blind") # does adaptive bottom table cost in play?
pair("A_lookahead","C_adapblind","lookahead","adaptive-blind")   # adaptive-side adopt view
PY
log "=== BURN 3-way BOTTOMING A/B DONE ($(stamp)) ==="
