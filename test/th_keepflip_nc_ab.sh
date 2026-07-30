#!/usr/bin/env bash
# NON-CLAIRVOYANT keep-flip A/B: the "real metric" validation for the TH keep-floor. Same paired
# force-keep vs table-mull design as th_keepflip_ab.sh, but the play arms run under MTG_NC_SEARCH
# (reshuffle-averaged, blind) so a keep-benefit that is really CLAIRVOYANT known-draw exploitation
# (e.g. the engine dodging a Treasure Hunt whiff it can foresee) collapses. If keep still beats mull
# under NC on the mulled good-payoff hands, the floor is a genuine blind improvement.  ~4.5x slower
# than clairvoyant, so N is smaller; still targets ~150 mulled good-payoff hands (B+C).
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg
D=decks/treasure_hunt/treasure_hunt.txt
P=decks/treasure_hunt/treasure_hunt.profile.json
SEED=${SEED:-9000001}
N=${N:-6000}
DEPTH=${DEPTH:-5}
BUDGET=${BUDGET:-20}
OUT=test/logs/th_keepflip_nc
SCAN="$OUT/scan"
mkdir -p "$SCAN"
echo "=== TH keep-flip NC A/B  seed=$SEED N=$N d$DEPTH b$BUDGET  MTG_NC_SEARCH=1  $(date -u +%H:%M:%S) ==="

echo "[1/3] classify scan (max-turns 0; keep decision, no NC needed) ..."
rm -f "$SCAN"/*.json
$BIN "$D" --profile "$P" --games "$N" --max-turns 0 --depth 0 --seed "$SEED" \
     --log-dir "$SCAN" --threads 0 >/dev/null 2>&1
python3 - "$SCAN" "$OUT/class.tsv" <<'PY'
import json, glob, sys, os, re
scan, out = sys.argv[1], sys.argv[2]
rows=[]
for fn in glob.glob(os.path.join(scan,'*.json')):
    gi=int(re.search(r'game_(\d+)\.json$', fn).group(1))
    d=json.load(open(fn)); ms=d.get('mulliganSequence')
    if not ms: continue
    h=[c['cardName'] for c in ms[0]['hand']]; kept=ms[0]['kept']
    th=h.count('Treasure Hunt'); rt='Reliquary Tower' in h; sk='Saprazzan Skerry' in h
    if th>=4: cls='A_4TH'
    elif th>=1 and rt: cls='B_TH_RT'
    elif th>=1 and sk: cls='C_TH_SK'
    elif th>=1: cls='D_1TH_plain'
    else: cls='Z_0TH'
    rows.append((gi,cls,1 if kept else 0))
rows.sort()
with open(out,'w') as f:
    for gi,cls,kept in rows: f.write(f"{gi}\t{cls}\t{kept}\n")
print(f"  classified {len(rows)} games -> {out}")
PY

run_arm(){ # $1=tag  $2..=extra args
  local tag=$1; shift
  MTG_NC_SEARCH=1 MTG_DUMP_WINS=1 $BIN "$D" --profile "$P" --games "$N" --seed "$SEED" \
    --depth "$DEPTH" --budget-ms "$BUDGET" --max-turns 8 --lookahead-bottoming --threads 0 "$@" \
    >"$OUT/agg_$tag.txt" 2>"$OUT/dump_$tag.err"
  grep -oE 'gi=[0-9]+ wt=-?[0-9]+' "$OUT/dump_$tag.err" \
    | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1\t\2/' | sort -n > "$OUT/wt_$tag.tsv"
  echo "  $tag: $(wc -l < "$OUT/wt_$tag.tsv") games; $(grep -i 'avg (turns)' "$OUT/agg_$tag.txt")"
}
echo "[2/3] KEEP arm (NC, force-keep every dealt 7) ..."
run_arm keep --force-mulligan "0:"
echo "[3/3] MULL arm (NC, table decides) ..."
run_arm mull

echo "=== NC RESULT (avg, unwon=9; delta = keep-mull, negative => keep better under BLIND play) ==="
python3 - "$OUT/class.tsv" "$OUT/wt_keep.tsv" "$OUT/wt_mull.tsv" <<'PY'
import sys, collections, math
cls={}
for ln in open(sys.argv[1]):
    gi,c,kept=ln.split(); cls[int(gi)]=(c,int(kept))
def load(fn):
    d={}
    for ln in open(fn):
        gi,wt=ln.split(); d[int(gi)]=int(wt)
    return d
keep=load(sys.argv[2]); mull=load(sys.argv[3])
def sc(wt): return wt if wt>0 else 9
def report(name, want):
    gis=[gi for gi,(c,k) in cls.items() if c in want and k==0 and gi in keep and gi in mull]
    if not gis: print(f"{name}: (no games)"); return
    diffs=[sc(keep[gi])-sc(mull[gi]) for gi in gis]
    n=len(diffs); m=sum(diffs)/n
    sd=math.sqrt(sum((x-m)**2 for x in diffs)/(n-1)) if n>1 else 0
    se=sd/math.sqrt(n) if n>1 else 0
    ka=sum(sc(keep[gi]) for gi in gis)/n; ma=sum(sc(mull[gi]) for gi in gis)/n
    print(f"{name}: n={n}  KEEP={ka:.3f}  MULL={ma:.3f}  delta={m:+.3f}  se={se:.3f}  t={m/se if se else 0:+.2f}")
report("B TH+Reliquary (mulled)", {'B_TH_RT'})
report("C TH+Skerry    (mulled)", {'C_TH_SK'})
report("pooled good-payoff (B+C)", {'B_TH_RT','C_TH_SK'})
report("D 1TH plain (mulled, control)", {'D_1TH_plain'})
PY
echo "DONE $(date -u +%H:%M:%S)"
