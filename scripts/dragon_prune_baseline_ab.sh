#!/bin/bash
# Prune OFF vs ON on the SHIPPING (baseline / fake-red) model -- cards.json as committed (Unclaimed held).
# OFF must reproduce committed GT exactly (byte-identical check); ON is the proposed new GT. Confirms the
# payoff-prune is a win independent of the Unclaimed fix. Covers Dragonstorm smoke (1001) + regression
# (2002/3003). Reports avg turn-to-go-off (unwon=9) + slower/faster counts per case.
set -e
cd /workspaces/MagicDeckTester2
DECK=decks/Dragonstorm/Dragonstorm.cod; PROF=decks/Dragonstorm/Dragonstorm.profile.json
OUT=logs/dragonstorm_prune_baseline; rm -rf $OUT; mkdir -p $OUT
# mode depth seed games budget
CASES=(
 "smoke 0 1001 1000 0" "smoke 3 1001 150 10" "smoke 5 1001 75 20"
 "regression 0 2002 1000 0" "regression 3 2002 300 10" "regression 3 3003 300 10"
 "regression 5 2002 250 20" "regression 5 3003 250 20"
)
runcase() { local cond=$1 env=$2 mode=$3 d=$4 s=$5 g=$6 b=$7
  local name="dragonstorm_${mode}_d${d}_s${s}"; local dir="$OUT/${cond}_${mode}_d${d}_s${s}"; mkdir -p "$dir"
  local JOB
  if [ "$d" -eq 5 ]; then
    JOB="{ \"name\":\"$name\",\"deck\":\"$DECK\",\"profile\":\"$PROF\",\"games\":$g,\"seed\":$s,\"budget_ms\":$b,\"weight\":0 }"
  else
    JOB="{ \"name\":\"$name\",\"deck\":\"$DECK\",\"profile\":\"$PROF\",\"games\":$g,\"seed\":$s,\"depth\":$d,\"budget_ms\":$b,\"ignore_play_profile\":true,\"weight\":0 }"
  fi
  echo "{ \"jobs\": [ $JOB ] }" > "$dir/m.json"
  env $env ./build/Release/mtg --batch "$dir/m.json" --threads 12 --game-log-dir "$dir/wins" >"$dir/out.txt" 2>&1
}
for c in "${CASES[@]}"; do set -- $c
  runcase OFF ""                        $1 $2 $3 $4 $5
  runcase ON  MTG_DRAGON_PAYOFF_PRUNE=1 $1 $2 $3 $4 $5
done
python3 - "$OUT" <<'PY'
import sys,os
OUT=sys.argv[1]
def load(p):
    d={}
    for line in open(p):
        a=line.split()
        if len(a)>=2: d[int(a[0])]= 9 if int(a[1])==-1 else int(a[1])
    return d
def digest(p):
    for line in open(p):
        if 'digest=' in line: return line.split('digest=')[1].split()[0]
    return '?'
gt={}
for line in open('test/regression_gt.txt'):
    if line.startswith('dragonstorm'): k,v=line.strip().split('='); gt[k]=v
cases=[("smoke","0","1001"),("smoke","3","1001"),("smoke","5","1001"),
       ("regression","0","2002"),("regression","3","2002"),("regression","3","3003"),
       ("regression","5","2002"),("regression","5","3003")]
print(f"{'case':26s} {'OFF':>7s} {'ON':>7s} {'ON-OFF':>8s}  worse better  OFF==GT?")
tot={'OFF':0,'ON':0,'n':0}
for mode,d,s in cases:
    name=f"dragonstorm_{mode}_d{d}_s{s}"
    OFFd=f"{OUT}/OFF_{mode}_d{d}_s{s}"; ONd=f"{OUT}/ON_{mode}_d{d}_s{s}"
    OFF=load(f"{OFFd}/wins/{name}.wins"); ON=load(f"{ONd}/wins/{name}.wins")
    keys=sorted(set(OFF)&set(ON))
    o=sum(OFF[k] for k in keys)/len(keys); n=sum(ON[k] for k in keys)/len(keys)
    worse=sum(1 for k in keys if ON[k]>OFF[k]); better=sum(1 for k in keys if ON[k]<OFF[k])
    offdig=digest(f"{OFFd}/out.txt"); gtdig=gt.get(name,'?/?').split('/')[1] if '/' in gt.get(name,'') else '?'
    match='MATCH' if offdig==gtdig else f'DIFF({offdig[:8]}/{gtdig[:8]})'
    print(f"{name:26s} {o:7.4f} {n:7.4f} {n-o:+8.4f}  {worse:5d} {better:6d}  {match}")
    w=len(keys); tot['OFF']+=o*w; tot['ON']+=n*w; tot['n']+=w
N=tot['n']; print(f"\nWEIGHTED MEAN  OFF(=GT)={tot['OFF']/N:.4f}  ON(prune)={tot['ON']/N:.4f}  ON-OFF={(tot['ON']-tot['OFF'])/N:+.4f}")
PY
echo "BASELINE_AB_DONE"
