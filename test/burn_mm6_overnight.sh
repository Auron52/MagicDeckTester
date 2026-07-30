#!/usr/bin/env bash
# ============================================================================================
# CHAINED OVERNIGHT for Burn: generate the mm6 (max_mull=6) exhaustive keep/bottom profile,
# then run the PURE NEW-vs-OLD full-profile A/B (avg9, loss=9) against the currently-shipped
# mm3 profile. NON-ADOPTING: it only stages + reports; the user decides adopt vs discard in the
# morning from the numbers (per the reporting rule).
#
# Why this shape (per user): we already SHIP a burn exhaustive profile, so the keep-vs-static and
# confounded-bottom isolations are first-profile checks we don't need. The only question is "is the
# new mm6 profile BETTER or WORSE than the mm3 we ship?" -> both arms run the FULL model (exhaustive
# keep AND that profile's own bottom table, MTG_EXHAUSTIVE_BOTTOM=1) + the deck's value.json; the ONLY
# variable is which profile file drives keep/bottom. MTG_EXHAUSTIVE_PROFILE=none makes --profile
# authoritative. Metric = avg turn-to-win, LOSS=9 (win% is a secondary column).
#
# Freeze (Rule 0): pins on HEAD at launch; the gen refuses to continue if HEAD moves. My uncommitted
# fallback-reserve change is env-OFF (MTG_VALUE_FALLBACK_RESERVE unset) => byte-identical play
# (verified 18/18 smoke), so the generated profile is valid for HEAD.
#
# Run detached (ready in the morning):
#   setsid nohup bash test/burn_mm6_overnight.sh </dev/null >logs/burn_mm6_overnight/chain.out 2>&1 &
#
# Knobs: BURN_TARGET_R (default 60), BURN_NSEEDS (8), BURN_GAMES (5000/seed).
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

BIN=./build/Release/mtg
DECK=decks/burn/burn.txt
STEM=burn
CARDS=src/cards/data/cards.json
VALJSON=decks/burn/burn.value.json
OLDGZ=decks/burn/burn.keepmodel.exhaustive.profile.json.gz
TARGET_R=${BURN_TARGET_R:-60}

OUT=logs/burn_mm6_overnight
ABOUT=logs/mm6_newvsold
mkdir -p "$OUT" "$ABOUT/$STEM"
LOG=$OUT/chain.out
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
say(){ echo "[$(stamp)] $*" | tee -a "$LOG"; }

[ -x "$BIN" ]      || { say "ABORT: build Release first ($BIN missing)"; exit 1; }
[ -f "$DECK" ]     || { say "ABORT: deck missing ($DECK)"; exit 1; }
[ -f "$OLDGZ" ]    || { say "ABORT: shipped OLD profile missing ($OLDGZ)"; exit 1; }
[ -f "$VALJSON" ]  || { say "ABORT: value.json missing ($VALJSON)"; exit 1; }

say "BURN mm6 overnight START (HEAD=$(git rev-parse --short HEAD), PID=$$, target_R=$TARGET_R)"

# ---- PHASE 1: generate the mm6 exhaustive profile (adaptive chunked gen) --------------------
say "PHASE 1: gen burn mm6 to R=$TARGET_R via test/exhaustive_chunked_gen.sh (maxmull=6 default)"
if KM_DECK="$DECK" KM_TARGET_R="$TARGET_R" KM_ROUND_R=5 bash test/exhaustive_chunked_gen.sh >> "$LOG" 2>&1; then
  say "PHASE 1 DONE"
else
  say "PHASE 1 FAILED (see $LOG and logs/${STEM}_gen/). ABORT before A/B."; exit 1
fi
NEW="logs/${STEM}_gen/pooled.profile.json"
[ -f "$NEW" ] || { say "ABORT: staged NEW profile missing ($NEW)"; exit 1; }

# ---- decompress the shipped OLD profile for a like-for-like arm -----------------------------
OLD="$ABOUT/old_${STEM}.profile.json"
if zcat "$OLDGZ" > "$OLD" 2>>"$LOG"; then say "OLD decompressed -> $OLD"; else say "ABORT: cannot decompress $OLDGZ"; exit 1; fi
say "arms: OLD=$OLD  NEW=$NEW  (only the keep/bottom profile differs)"

# ---- PHASE 2: NEW-vs-OLD full-profile A/B --------------------------------------------------
NSEEDS=${BURN_NSEEDS:-8}
SEEDS=$(seq 200000 811 $((200000 + 811*(NSEEDS-1))))
DEPTHS="3 5"
GAMES=${BURN_GAMES:-5000}
say "PHASE 2: A/B nseeds=$NSEEDS depths[$DEPTHS] games/seed=$GAMES  (both arms: full keep+bottom, MTG_EXHAUSTIVE_BOTTOM=1, value.json attached)"

run_arm(){ local prof="$1" tag="$2" d s
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_EXHAUSTIVE_PROFILE=none MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=1 MTG_VALUE_PROFILE="$VALJSON" \
      "$BIN" "$DECK" --profile "$prof" --seed "$s" --games "$GAMES" --depth "$d" --budget-ms 20 \
      --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$ABOUT/$STEM/wins_${tag}_d${d}_s${s}.out" 2> "$ABOUT/$STEM/err_${tag}_d${d}_s${s}.txt"
  done; done; }

run_arm "$OLD" old; say "OLD arm done"
run_arm "$NEW" new; say "NEW arm done"

# ---- report: avg9 (loss=9) OLD vs NEW, per depth + overall ----------------------------------
say "PHASE 2 REPORT (also appended to $OUT/REPORT.txt):"
: > "$OUT/REPORT.txt"
python3 - "$ABOUT/$STEM" "$DEPTHS" "$SEEDS" "$GAMES" "$STEM" <<'PY' | tee -a "$OUT/REPORT.txt" | tee -a "$LOG"
import sys,os
D,depths,seeds,GAMES,stem=sys.argv[1],[int(x) for x in sys.argv[2].split()],[int(x) for x in sys.argv[3].split()],int(sys.argv[4]),sys.argv[5]
LOSS=9   # max-turns=8 -> a game not won by T8 is a loss, scored as turn 9 (THE metric)
def wins(tag,d,s):
    fn=f"{D}/err_{tag}_d{d}_s{s}.txt"; w={}
    if not os.path.exists(fn): return w
    for ln in open(fn):
        if ln.startswith("[win]"):
            p=ln.split(); gi=int(p[1].split('=')[1]); wt=int(p[2].split('=')[1])
            if wt>0: w[gi]=wt
    return w
def mean(xs): return sum(xs)/len(xs) if xs else 0.0
def avg9(w): return (sum(w.values()) + LOSS*(GAMES-len(w)))/GAMES
print(f"\n===== {stem}: NEW(mm6) vs OLD(mm3)  full keep+bottom; only the profile differs =====")
print(f"  metric = avg turn-to-win, LOSS scored as {LOSS} (lower = better)")
print(f"{'depth':<6}{'OLD avg9':>10}{'NEW avg9':>10}{'delta':>9}   {'seedsNEW<':>9}{'O>L':>6}{'L>N':>6}   {'OLD win%':>9}{'NEW win%':>9}")
g=[]
for d in depths:
    oa=[]; na=[]; reg=0; gain=0; better=0; ow=[]; nw=[]
    for s in seeds:
        wo=wins("old",d,s); wn=wins("new",d,s)
        a=avg9(wo); b=avg9(wn); oa.append(a); na.append(b)
        if b<a-1e-9: better+=1
        ow.append(len(wo)/GAMES*100); nw.append(len(wn)/GAMES*100)
        for gi in set(wo)|set(wn):
            o=gi in wo; n=gi in wn
            if o and not n: reg+=1
            elif n and not o: gain+=1
    OA=mean(oa); NA=mean(na); g.append(NA-OA)
    print(f"d{d:<5}{OA:>10.4f}{NA:>10.4f}{NA-OA:>+9.4f}   {str(better)+'/'+str(len(seeds)):>9}{reg:>6}{gain:>6}   {mean(ow):>9.2f}{mean(nw):>9.2f}")
gm=mean(g)
verdict=("NEW BETTER" if gm<-0.002 else "NEW WORSE" if gm>0.002 else "NEUTRAL (~tie)")
print(f"  overall delta (avg9, avg over depths): {gm:+.4f}  -> {verdict}   (negative = NEW better)")
print(f"  NOTE: NOT adopted. To adopt: gzip -c {D.rsplit('/',2)[0]}/../{stem}_gen/pooled.profile.json > decks/{stem}/{stem}.keepmodel.exhaustive.profile.json.gz  (+ raw.json.gz), then rebaseline GT.")
PY

say "BURN mm6 overnight COMPLETE. Profile STAGED (logs/${STEM}_gen/pooled.profile.json), NOT adopted. See $OUT/REPORT.txt"
