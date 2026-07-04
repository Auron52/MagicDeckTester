#!/usr/bin/env bash
# 3-way in-game A/B for burn (=test_deck): the KEEP decision under three profiles, bottoming OFF on all
# (MTG_EXHAUSTIVE_BOTTOM=0) so the ONLY difference is the mulligan keep table:
#   static   -- decks/test_deck.profile.json                         (the static keep rule)
#   uniform  -- exhaustive keep, uniform R=100                       (full-cost exhaustive)
#   adaptive -- exhaustive keep, adaptive R=100 (confidence-driven)  (the sampling under test)
# Two questions:
#   (1) adaptive vs uniform  -> is the adaptive SAMPLING lossless in actual play? (expect ~0)
#   (2) {uniform,adaptive} vs static -> is exhaustive keep worth it on burn? (uniform R=100 report: -0.092t)
# Mirrors keepmodel_exhaustive_ab.sh flags EXACTLY for comparability. Read-only w.r.t. profiles.
set -u
BIN=./build/Release/mtg
DECK=decks/test_deck.txt
STATIC=decks/test_deck.profile.json
UNIFORM=decks/test_deck.keepmodel.exhaustive.uniform.r100.profile.json
ADAPTIVE=decks/test_deck.keepmodel.exhaustive.adaptive.r100.profile.json
for f in "$STATIC" "$UNIFORM" "$ADAPTIVE"; do [ -f "$f" ] || { echo "missing $f"; exit 1; }; done

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}

OUT=logs/keepmodel_exh_burn_3way; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== BURN 3-way KEEP A/B ($(stamp)) ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=20 max-turns=8 (MTG_EXHAUSTIVE_BOTTOM=0 on all)"

ab(){ local prof="$1" tag="$2"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=0 "$BIN" "$DECK" --profile "$prof" --seed "$s" \
      --games "$GAMES" --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.wins" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

log "--- run static ($(stamp)) ---";   ab "$STATIC"   static
log "--- run uniform ($(stamp)) ---";  ab "$UNIFORM"  uniform
log "--- run adaptive ($(stamp)) ---"; ab "$ADAPTIVE" adaptive

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
def pair(base,cand):
    print(f"\n=== {cand} vs {base} (negative delta = {cand} wins earlier) ===")
    print(f"{'depth':<6}{'won_'+base:>12}{'won_'+cand:>12}{base+'_D':>9}{cand+'_D':>9}{'meanD':>9}{'seedsD<0':>10}")
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
        print(f"d{d:<5}{wb_tot:>12}{wc_tot:>12}{am:>9.3f}{cm:>9.3f}{delta:>+9.4f}{str(nlt)+'/'+str(len(seeds)):>10}")
    gm=mean(grand)
    print(f"overall meanD: {gm:>+.4f}  ({cand+' BEATS '+base if gm<0 else cand+' loses to '+base})")
pair("static","uniform")
pair("static","adaptive")
pair("uniform","adaptive")
PY
log "=== BURN 3-way A/B DONE ($(stamp)) ==="
