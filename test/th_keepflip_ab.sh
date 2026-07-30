#!/usr/bin/env bash
# TH keep-flip A/B: for the good-payoff hands the keep table MULLS (TH+Reliquary Tower,
# TH+Saprazzan Skerry, 4xTH), measure whether KEEPING them beats mulliganing, in real
# depth-5 play. Metric = avg (mean turn-to-win, unwon = max_turns+1=9; lower is better).
#
# Method (paired by game index gi; same base seed => same initial deal both arms):
#   1) classify: max-turns 0 scan -> each gi's initial-7 class + the table's keep/mull.
#   2) KEEP arm  : --force-mulligan "0:" (keep every dealt 7)      -> per-gi win turn.
#   3) MULL arm  : normal play (table mulls the marginal hands)    -> per-gi win turn.
#   4) On the gi's the table MULLS in a target class, compare KEEP vs MULL avg.
# Negative (keep-mull) => keeping is better => the table over-mulls that class.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg
D=decks/treasure_hunt/treasure_hunt.txt
P=decks/treasure_hunt/treasure_hunt.profile.json
SEED=${SEED:-7000001}
N=${N:-16000}
DEPTH=${DEPTH:-5}
BUDGET=${BUDGET:-20}
OUT=test/logs/th_keepflip
SCAN="$OUT/scan"
mkdir -p "$SCAN"
echo "=== TH keep-flip A/B  seed=$SEED N=$N d$DEPTH b$BUDGET  $(date -u +%H:%M:%S) ==="

echo "[1/3] classify scan (max-turns 0) ..."
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
  MTG_DUMP_WINS=1 $BIN "$D" --profile "$P" --games "$N" --seed "$SEED" \
    --depth "$DEPTH" --budget-ms "$BUDGET" --max-turns 8 --lookahead-bottoming --threads 0 "$@" \
    >"$OUT/agg_$tag.txt" 2>"$OUT/dump_$tag.err"
  grep -oE 'gi=[0-9]+ wt=-?[0-9]+' "$OUT/dump_$tag.err" \
    | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1\t\2/' | sort -n > "$OUT/wt_$tag.tsv"
  echo "  $tag: $(wc -l < "$OUT/wt_$tag.tsv") games -> avg line: $(grep -i 'avg (turns)' "$OUT/agg_$tag.txt")"
}
echo "[2/3] KEEP arm (force-keep every dealt 7) ..."
run_arm keep --force-mulligan "0:"
echo "[3/3] MULL arm (table decides) ..."
run_arm mull

echo "=== RESULT (avg, unwon=9; delta = keep-mull, negative => keep better) ==="
python3 - "$OUT/class.tsv" "$OUT/wt_keep.tsv" "$OUT/wt_mull.tsv" <<'PY'
import sys, collections
cls={}
for ln in open(sys.argv[1]):
    gi,c,kept=ln.split(); cls[int(gi)]=(c,int(kept))
def load(fn):
    d={}
    for ln in open(fn):
        gi,wt=ln.split(); d[int(gi)]=int(wt)
    return d
keep=load(sys.argv[2]); mull=load(sys.argv[3])
def score(wt): return wt if wt>0 else 9
# group gi's by (class, kept-by-table)
groups=collections.defaultdict(list)
for gi,(c,kept) in cls.items():
    groups[(c,kept)].append(gi)
def avg(gis, table):
    xs=[score(table[gi]) for gi in gis if gi in table]
    return (sum(xs)/len(xs), len(xs)) if xs else (float('nan'),0)
print(f"{'class':14} {'table':5} {'n':>5} {'KEEP avg':>9} {'MULL avg':>9} {'delta':>8}  {'kept/won':>9}")
order=['A_4TH','B_TH_RT','C_TH_SK','D_1TH_plain','Z_0TH']
for c in order:
    for kept in (0,1):
        gis=groups.get((c,kept),[])
        if not gis: continue
        ka,kn=avg(gis,keep); ma,mn=avg(gis,mull)
        tag='MULL' if kept==0 else 'keep'
        # only the delta on MULLED (kept==0) hands answers "should we have kept?"
        star=' <== keeper?' if (kept==0 and ka<ma-0.02) else (' (mull ok)' if kept==0 and ka>ma+0.02 else '')
        print(f"{c:14} {tag:5} {kn:>5} {ka:>9.3f} {ma:>9.3f} {ka-ma:>+8.3f}{star}")
PY
echo "DONE $(date -u +%H:%M:%S)"
