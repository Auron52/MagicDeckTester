#!/bin/bash
# Per-deck regression-suite audit of an adopted keep model: runs the deck's REGRESSION_CASES
# (seeds 2002/3003, depths 0/3/5 -- DISJOINT from the A/B fit seeds) with the keepmodel profile via
# the SAME --batch path the harness uses, then diffs per-game vs the committed static baseline in
# test/gt_logs/. Classifies every flip (won<->loss, faster, slower). Read-only; promotes nothing.
#
# Usage: DECK=burn PROF=logs/.../test_deck.keepmodel.profile.json bash test/keepmodel_regression_audit.sh
#        (optional) BASELINE=1 also runs the STATIC profile fresh to prove it reproduces gt_logs (rule 6).
set -uo pipefail
cd "$(dirname "$0")/.."
source test/regression_cases.sh
DECK=${DECK:?}; PROF=${PROF:?}
BIN=./build/Release/mtg
OUT=logs/keepmodel_regression_audit/$DECK; mkdir -p "$OUT/wins"
file=${DECK_FILE[$DECK]}; static=${DECK_PROF[$DECK]}

# Collect this deck's regression cases.
cases=(); for spec in "${REGRESSION_CASES[@]}"; do set -- $spec; [ "$1" = "$DECK" ] && cases+=("$spec"); done
[ ${#cases[@]} -eq 0 ] && { echo "no regression cases for $DECK"; exit 1; }

build_manifest(){ # $1=profile $2=outfile  (save args BEFORE the per-case `set --` clobbers $1/$2)
  local prof="$1" outf="$2"
  { echo '{ "jobs": ['; first=1
    for spec in "${cases[@]}"; do set -- $spec; d=$2; s=$3; g=$4; b=$5
      [ "$d" -gt 0 ] && bud=$b || bud=0
      name="${DECK}_regression_d${d}_s${s}"
      [ $first -eq 1 ] && first=0 || printf ',\n'
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "depth": %s, "budget_ms": %s }' \
        "$name" "$file" "$prof" "$g" "$s" "$d" "$bud"
    done; printf '\n] }\n'; } > "$outf"
}

run(){ # $1=profile $2=winsdir
  mkdir -p "$2"; local man="$OUT/manifest_$(basename "$2").json"
  build_manifest "$1" "$man"
  "$BIN" --batch "$man" --threads 0 --game-log-dir "$2" >/dev/null 2>&1
}

if [ "${BASELINE:-0}" = 1 ]; then
  echo "[$DECK] running STATIC arm to verify it reproduces gt_logs (rule 6)..."
  run "$static" "$OUT/wins_static"
fi
echo "[$DECK] running KEEPMODEL arm ($PROF)..."
run "$PROF" "$OUT/wins"

python3 - "$DECK" "$OUT" "${BASELINE:-0}" <<'PY'
import sys,os,glob
deck,out,baseline=sys.argv[1],sys.argv[2],sys.argv[3]
def load(fn):
 m={}
 if os.path.exists(fn):
  for ln in open(fn):
   a=ln.split()
   if len(a)>=2: m[int(a[0])]=int(a[1])
 return m
keys=sorted({os.path.basename(f)[:-5] for f in glob.glob(f"{out}/wins/*.wins")})
H=8
print(f"\n===== {deck}: keepmodel vs committed-static (gt_logs) =====")
agg={'faster':0,'slower':0,'WON->LOST':0,'LOST->WON':0}
for k in keys:
 gt=load(f"test/gt_logs/{k}.wins"); nw=load(f"{out}/wins/{k}.wins")
 if baseline=='1':
  st=load(f"{out}/wins_static/{k}.wins")
  mism=sum(1 for g in gt if g in st and st[g]!=gt[g])
  print(f"  [{k}] static-vs-gt mismatches={mism} (want 0)")
 gis=sorted(set(gt)&set(nw));
 fl={'faster':[], 'slower':[], 'WON->LOST':[], 'LOST->WON':[]}
 dreg=0
 for g in gis:
  o,n=gt[g],nw[g]
  ro=o if o>0 else H+1; rn=n if n>0 else H+1; dreg+=rn-ro
  if o==n: continue
  if o>0 and n<0: fl['WON->LOST'].append(g)
  elif o<0 and n>0: fl['LOST->WON'].append(g)
  elif o>0 and n>0 and n<o: fl['faster'].append(g)
  elif o>0 and n>0 and n>o: fl['slower'].append(g)
 for t in fl: agg[t]+=len(fl[t])
 nch=sum(len(v) for v in fl.values())
 print(f"  [{k}] n={len(gis)} changed={nch} regretΔ={dreg/len(gis):+.4f} | faster={len(fl['faster'])} slower={len(fl['slower'])} LOST->WON={len(fl['LOST->WON'])} WON->LOST={len(fl['WON->LOST'])}"+ (f"  WL_games={fl['WON->LOST']}" if fl['WON->LOST'] else ""))
print(f"  TOTAL: faster={agg['faster']} slower={agg['slower']} LOST->WON={agg['LOST->WON']} WON->LOST={agg['WON->LOST']}")
PY
