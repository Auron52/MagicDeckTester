#!/usr/bin/env bash
# CONFOUNDED bottoming A/B for burn: same two arms as the bottoming 3-way, but MTG_CONFOUND_BOTTOM=1 on
# BOTH -- the library is reshuffled AFTER the bottoming decision, so the clairvoyant lookahead's peek at
# the exact library is worthless. The only remaining difference is the KEPT HAND (the bottoming decision).
#   A  uniform keep + lookahead bottoming   (peek nullified by the reshuffle)
#   B  uniform keep + blind exhaustive R=100 bottoming  (never peeked; chosen as argmin over fresh shuffles)
# Expectation (per the argmin argument): B >= A now, reversing the non-confounded +0.076t "loss".
# Both arms confounded => paired on the SAME fresh library per game; comparable to logs/keepmodel_exh_burn_bottom3.
set -u
BIN=./build/Release/mtg
DECK=decks/test_deck.txt
UNIFORM=decks/test_deck.keepmodel.exhaustive.uniform.r100.profile.json
[ -f "$UNIFORM" ] || { echo "missing $UNIFORM"; exit 1; }

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}

OUT=logs/keepmodel_exh_burn_confound; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== BURN CONFOUNDED bottoming A/B ($(stamp)) ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=20 max-turns=8 (MTG_CONFOUND_BOTTOM=1 both arms)"

# ab <tag> <exhaustive_bottom 0|1>
ab(){ local tag="$1" exb="$2"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_DUMP_WINS=1 MTG_CONFOUND_BOTTOM=1 MTG_EXHAUSTIVE_BOTTOM="$exb" "$BIN" "$DECK" --profile "$UNIFORM" --seed "$s" \
      --games "$GAMES" --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.wins" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

log "--- A: lookahead bottoming (confounded) ($(stamp)) ---"; ab A_lookahead 0
log "--- B: blind exhaustive bottoming (confounded) ($(stamp)) ---"; ab B_blind 1

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
print(f"\n=== blind vs lookahead, BOTH CONFOUNDED (negative delta = blind wins earlier) ===")
print(f"{'depth':<6}{'won_look':>10}{'won_blind':>10}{'lookD':>9}{'blindD':>9}{'meanD':>9}{'seedsD<0':>10}")
grand=[]
for d in depths:
    wl=wbd=0; lms=[]; bms=[]; sdel=[]
    for s in seeds:
        wa=wins("A_lookahead",d,s); wb=wins("B_blind",d,s)
        av=[t for t in wa.values() if t>0]; bv=[t for t in wb.values() if t>0]
        wl+=len(av); wbd+=len(bv)
        am=mean(av); bm=mean(bv); lms.append(am); bms.append(bm); sdel.append(bm-am)
    am=mean(lms); bm=mean(bms); delta=bm-am; nlt=sum(1 for x in sdel if x<0)
    grand.append(delta)
    print(f"d{d:<5}{wl:>10}{wbd:>10}{am:>9.3f}{bm:>9.3f}{delta:>+9.4f}{str(nlt)+'/'+str(len(seeds)):>10}")
gm=mean(grand)
print(f"overall meanD: {gm:>+.4f}  ({'blind BEATS lookahead' if gm<0 else 'blind loses to lookahead'})")
print("(compare non-confounded logs/keepmodel_exh_burn_bottom3: blind lost +0.0488 overall, +0.076 at d3/d5)")
PY
log "=== BURN CONFOUNDED A/B DONE ($(stamp)) ==="
