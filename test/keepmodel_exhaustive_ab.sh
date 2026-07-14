#!/usr/bin/env bash
# In-game A/B for the EXHAUSTIVE bucketed keep/bottom policy (see docs/design/exhaustive-keep-policy.md
# and analyzer MTG_KEEP_EXHAUSTIVE). Two disjoint isolations, selected by KM_MODE:
#
#   KM_MODE=keep    (default)  exhaustive KEEP profile  vs  static profile.
#                              Bottoming held IDENTICAL (lookahead) on both sides -- MTG_EXHAUSTIVE_BOTTOM
#                              is OFF everywhere -- so the only difference is the mulligan KEEP decision.
#                              This is the definitive check of the R=20 analyzer gap (D_static-D_opt).
#
#   KM_MODE=bottom             exhaustive profile with blind exhaustive BOTTOMING (MTG_EXHAUSTIVE_BOTTOM=1)
#                              vs the SAME exhaustive profile with clairvoyant lookahead bottoming (flag
#                              off). KEEP is exhaustive on both sides, so the only difference is the
#                              BOTTOMING policy: blind-optimal vs clairvoyant-lookahead. Lower (earlier
#                              win) is better; if blind is >= lookahead we may adopt it for realism.
#
# Mirrors keepmodel_ab_widseeds.sh's engine/flags/budget EXACTLY so numbers are comparable. Read-only
# w.r.t. deck profiles; all output under logs/keepmodel_exh_<mode>_<stem>/. Requires the exhaustive
# profile to already exist (generate via MTG_KEEP_EXHAUSTIVE); it is NOT regenerated here.
#
#   KM_DECK=decks/slivers_vial/slivers_vial.txt KM_MODE=keep bash test/keepmodel_exhaustive_ab.sh
set -u
BIN=./build/Release/mtg
DECK=${KM_DECK:?set KM_DECK=decks/<name>.txt}
MODE=${KM_MODE:-keep}
CARDS=${KM_CARDS:-src/cards/data/cards.json}
STEM=$(basename "$DECK"); STEM=${STEM%.*}   # strip ANY extension (.txt or .cod) -> deck stem
STATIC=${KM_STATIC:-decks/$STEM.profile.json}
EXH=${KM_EXH_PROFILE:-decks/$STEM.keepmodel.exhaustive.profile.json}

[ -f "$EXH" ]    || { echo "missing exhaustive profile: $EXH (generate with MTG_KEEP_EXHAUSTIVE=1)"; exit 1; }
[ -f "$STATIC" ] || { echo "missing static profile: $STATIC"; exit 1; }

# Same seed/depth/games grid as the wide-seed keep A/B for comparability.
SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}

OUT=logs/keepmodel_exh_${MODE}_$STEM; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== EXHAUSTIVE keep/bottom A/B ($(stamp)) deck=$STEM mode=$MODE ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=20 max-turns=8"
log "exhaustive profile: $EXH"

# ab <profile> <tag> <exhaustive_bottom 0|1>
# MTG_EXHAUSTIVE_PROFILE=none suppresses presence-gated auto-attach so the STATIC arm is genuinely
# static even when this deck has an adopted decks/<deck>.keepmodel.exhaustive sidecar. It's a no-op for
# the exhaustive arms: those load the block directly via --profile, and AttachExhaustiveSidecar returns
# early on an already-populated block before it ever consults the env. Replaces the old mv/scratch-path
# workaround for A/B baseline contamination.
ab(){ local prof="$1" tag="$2" exb="$3"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_EXHAUSTIVE_PROFILE=none MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM="$exb" "$BIN" "$DECK" --profile "$prof" --seed "$s" \
      --games "$GAMES" --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.wins" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

if [ "$MODE" = keep ]; then
  # KEEP isolation: exhaustive keep vs static; exhaustive bottoming OFF on BOTH (lookahead bottoming).
  A_PROF=$STATIC; A_TAG=static; A_EXB=0
  B_PROF=$EXH;    B_TAG=exh;    B_EXB=0
else
  # BOTTOM isolation: exhaustive keep on BOTH; toggle exhaustive bottoming. A=lookahead, B=blind.
  A_PROF=$EXH; A_TAG=lookahead; A_EXB=0
  B_PROF=$EXH; B_TAG=exhbottom; B_EXB=1
fi

log "--- A/B baseline=$A_TAG ($(stamp)) ---";   ab "$A_PROF" "$A_TAG" "$A_EXB"
log "--- A/B candidate=$B_TAG ($(stamp)) ---";  ab "$B_PROF" "$B_TAG" "$B_EXB"

python3 - "$OUT" "$A_TAG" "$B_TAG" "$DEPTHS" "$SEEDS" <<'PY' | tee -a "$REPORT"
import sys,os
OUT,base,cand=sys.argv[1],sys.argv[2],sys.argv[3]
depths=[int(x) for x in sys.argv[4].split()]; seeds=[int(x) for x in sys.argv[5].split()]
def wins(tag,d,s):
    # MTG_DUMP_WINS emits "[win] gi=N wt=M" to STDERR (the err_ file); the .wins/stdout holds only the
    # summary. Parse the err_ file.
    fn=f"{OUT}/err_{tag}_d{d}_s{s}.txt"; w={}
    if not os.path.exists(fn): return w
    for ln in open(fn):
        if ln.startswith("[win]"):
            p=ln.split(); gi=int(p[1].split('=')[1]); wt=int(p[2].split('=')[1]); w[gi]=wt
    return w
def mean(xs): return sum(xs)/len(xs) if xs else 0.0
print(f"\n=== EXHAUSTIVE A/B ({cand} vs {base}; negative delta = {cand} wins earlier) ===")
print(f"{'depth':<6}{base:>10}{cand:>10}{'meanD':>10}{base[:3]+'->L':>8}{'L->'+cand[:3]:>8}{'seedsD<0':>10}")
grand=[]
for d in depths:
    dd=[]; wl=0; lw=0; sdel=[]
    for s in seeds:
        wa=wins(base,d,s); wb=wins(cand,d,s)
        av=[t for t in wa.values() if t>0]; bv=[t for t in wb.values() if t>0]
        am=mean(av); bm=mean(bv); sdel.append(bm-am)
        for gi in wa:
            a=wa.get(gi,0); b=wb.get(gi,0); aw=a>0; bw=b>0
            if aw and not bw: wl+=1
            elif bw and not aw: lw+=1
        dd.append((am,bm))
    am=mean([x[0] for x in dd]); bm=mean([x[1] for x in dd]); delta=bm-am
    nlt=sum(1 for x in sdel if x<0); grand.append(delta)
    print(f"d{d:<5}{am:>10.3f}{bm:>10.3f}{delta:>+10.4f}{wl:>8}{lw:>8}{str(nlt)+'/'+str(len(seeds)):>10}")
gm=mean(grand)
print(f"\noverall meanD (avg over depths): {gm:>+.4f}  ({cand+' BEATS '+base if gm<0 else cand+' loses to '+base})")
PY
log "=== EXHAUSTIVE A/B DONE ($(stamp)) ==="
