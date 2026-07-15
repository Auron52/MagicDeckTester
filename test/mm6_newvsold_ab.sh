#!/usr/bin/env bash
# ============================================================================================
# DIRECT NEW-vs-OLD full-profile A/B for the mm6 staged profiles.
#
# The point (per user): once you already SHIP an exhaustive profile, the keep-vs-static and
# confounded-bottom isolations are no longer the question. The only question is: does the NEW
# profile play BETTER or WORSE than the one we currently ship? So both arms run the FULL model
# (exhaustive keep AND that profile's own bottom table, MTG_EXHAUSTIVE_BOTTOM=1) and the ONLY
# variable is which profile file drives the decisions:
#   OLD  = the currently-committed decks/<stem>/<stem>.keepmodel.exhaustive.profile.json.gz  (decompressed)
#   NEW  = the staged logs/<stem>_gen/pooled.profile.json  (this chain's mm6 output)
#
# MTG_EXHAUSTIVE_PROFILE=none makes --profile authoritative (no double auto-attach of the
# committed sidecar). Engine/flags/budget mirror the other keep A/Bs so numbers are comparable.
# Large seed set (default 48) for a lot of game data. Read-only; output under logs/mm6_newvsold/.
#
# Run detached:
#   setsid nohup bash test/mm6_newvsold_ab.sh </dev/null >logs/mm6_newvsold/chain.out 2>&1 &
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg
OUT=logs/mm6_newvsold
mkdir -p "$OUT"
CHAIN=$OUT/chain.out
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
say(){ echo "[$(stamp)] $*" | tee -a "$CHAIN"; }

# Cheap decks first so signal arrives early; TH (search-heavy) last.
ORDER=(slivers_vial Knights treasure_hunt)
declare -A DECKF=(
  [treasure_hunt]=decks/treasure_hunt/treasure_hunt.txt
  [slivers_vial]=decks/slivers_vial/slivers_vial.txt
  [Knights]=decks/Knights/Knights.cod
)

# BATCHED grid: few seeds x many games = same 48k games/arm/depth, but ~6x fewer profile loads.
# The 206M OLD profile costs ~65s to PARSE per process launch; batching kills that as the dominant tax.
# Each run already uses all cores (--threads 0) across its games, so fewer/bigger runs lose no parallelism.
# 8 seeds x 6000 games = 48000 games/arm/depth (unchanged data). Override with MM_SEEDS / MM_NSEEDS / MM_GAMES.
NSEEDS=${MM_NSEEDS:-8}
SEEDS=${MM_SEEDS:-$(seq 200000 811 $((200000 + 811*(NSEEDS-1))))}
DEPTHS=${MM_DEPTHS:-"3 5"}
GAMES_DEFAULT=${MM_GAMES:-6000}
# Per-deck games/seed. User: keep them approximately EVEN, ~5000+ each, deadline (~19:30 PDT) is soft.
# So 5000/seed for all three = 40k/arm/depth each (8 seeds x 2 depths x 2 arms x 5000). Large, even sample.
# Est finish ~19:10-19:30 PDT from ~15:16. Clean rates: Sliv ~26ms, Kn ~19ms, TH ~38-45ms/game.
declare -A GAMES_BY=( [treasure_hunt]=5000 [slivers_vial]=5000 [Knights]=5000 )
CARDS=src/cards/data/cards.json

say "NEW-vs-OLD full-profile A/B start (HEAD=$(git rev-parse --short HEAD), PID=$$)"
say "decks[${ORDER[*]}] nseeds=$(echo $SEEDS | wc -w) depths[$DEPTHS] games/seed(TH=${GAMES_BY[treasure_hunt]},Slivers=${GAMES_BY[slivers_vial]},Knights=${GAMES_BY[Knights]})  (both arms: full keep+bottom, MTG_EXHAUSTIVE_BOTTOM=1)"

# run <profile> <tag> <deck> <stem>  -> games for every depth/seed to $OUT/<stem>/
run_arm(){ local prof="$1" tag="$2" deck="$3" stem="$4"; local d s
  mkdir -p "$OUT/$stem"
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_EXHAUSTIVE_PROFILE=none MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=1 "$BIN" "$deck" \
      --profile "$prof" --seed "$s" --games "$GAMES" --depth "$d" --budget-ms 20 \
      --max-turns 8 --lookahead-bottoming --threads 0 \
      > "$OUT/$stem/wins_${tag}_d${d}_s${s}.out" 2> "$OUT/$stem/err_${tag}_d${d}_s${s}.txt"
  done; done; }

report_deck(){ local stem="$1"
python3 - "$OUT/$stem" "$DEPTHS" "$SEEDS" "$GAMES" "$stem" <<'PY' | tee -a "$OUT/REPORT.txt"
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
def avg9(w):  # average turn over ALL GAMES games; losses = LOSS
    return (sum(w.values()) + LOSS*(GAMES-len(w)))/GAMES
print(f"\n===== {stem}: NEW vs OLD (full keep+bottom; only the profile differs) =====")
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
            if o and not n: reg+=1        # OLD won, NEW lost  = regression
            elif n and not o: gain+=1     # NEW won, OLD lost  = gain
    OA=mean(oa); NA=mean(na); g.append(NA-OA)
    print(f"d{d:<5}{OA:>10.4f}{NA:>10.4f}{NA-OA:>+9.4f}   {str(better)+'/'+str(len(seeds)):>9}{reg:>6}{gain:>6}   {mean(ow):>9.2f}{mean(nw):>9.2f}")
gm=mean(g)
verdict=("NEW BETTER" if gm<-0.002 else "NEW WORSE" if gm>0.002 else "NEUTRAL (~tie)")
print(f"  overall delta (avg9, avg over depths): {gm:+.4f}  -> {verdict}   (negative = NEW better)")
PY
}

: > "$OUT/REPORT.txt"
for stem in "${ORDER[@]}"; do
  deck="${DECKF[$stem]}"; new="logs/${stem}_gen/pooled.profile.json"; old="$OUT/old_$stem.profile.json"
  [ -f "$new" ] || { say "SKIP $stem: staged NEW missing ($new)"; continue; }
  [ -f "$old" ] || { say "SKIP $stem: decompressed OLD missing ($old)"; continue; }
  GAMES=${GAMES_BY[$stem]:-$GAMES_DEFAULT}   # per-deck games/seed
  say "########## $stem START (deck=$deck, games/seed=$GAMES, x$NSEEDS seeds x $(echo $DEPTHS|wc -w) depths x2 arms) ##########"
  run_arm "$old" old "$deck" "$stem"; say "$stem OLD arm done"
  run_arm "$new" new "$deck" "$stem"; say "$stem NEW arm done"
  say "$stem REPORT:"; report_deck "$stem"
  say "########## $stem DONE ##########"
done
say "NEW-vs-OLD A/B COMPLETE. See $OUT/REPORT.txt"
