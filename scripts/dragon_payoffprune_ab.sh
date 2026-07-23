#!/bin/bash
# Dragonstorm payoff-prune A/B across the regression cases, 3 conditions on ONE binary:
#   A = baseline cards (Unclaimed-off) + prune off  == committed GT reference
#   C = fix cards (Unclaimed-on)     + prune off  == the "regression"
#   B = fix cards (Unclaimed-on)     + prune on   == the proposal (Unclaimed fix + payoff-prune)
# Reports avg turn-to-go-off (unwon = max_turns+1 = 9) + wall time (perf proxy) per case+condition.
set -e
cd /workspaces/MagicDeckTester2
DECK=decks/Dragonstorm/Dragonstorm.cod; PROF=decks/Dragonstorm/Dragonstorm.profile.json
CJ=src/cards/data/cards.json; CB=logs/unclaimed_ab/cards_base_HEAD.json
OUT=logs/dragonstorm_payoffprune; rm -rf $OUT; mkdir -p $OUT
CASES=("0 2002 1000 0" "3 2002 300 10" "3 3003 300 10" "5 2002 250 20" "5 3003 250 20")
runcase() { # cond cards env d s g b
  local cond=$1 cards=$2 env=$3 d=$4 s=$5 g=$6 b=$7
  local name="dragonstorm_regression_d${d}_s${s}"
  local dir="$OUT/${cond}_d${d}_s${s}"; mkdir -p "$dir"
  local JOB
  if [ "$d" -eq 5 ]; then
    JOB="{ \"name\":\"$name\",\"deck\":\"$DECK\",\"profile\":\"$PROF\",\"games\":$g,\"seed\":$s,\"budget_ms\":$b,\"weight\":0 }"
  else
    JOB="{ \"name\":\"$name\",\"deck\":\"$DECK\",\"profile\":\"$PROF\",\"games\":$g,\"seed\":$s,\"depth\":$d,\"budget_ms\":$b,\"ignore_play_profile\":true,\"weight\":0 }"
  fi
  echo "{ \"jobs\": [ $JOB ] }" > "$dir/m.json"
  local t0=$(date +%s.%N)
  env $env ./build/Release/mtg --batch "$dir/m.json" --threads 12 --cards-json "$cards" --game-log-dir "$dir/wins" >"$dir/out.txt" 2>&1
  local t1=$(date +%s.%N)
  echo "$t1-$t0" | bc > "$dir/wall.txt"
}
for c in "${CASES[@]}"; do
  set -- $c
  runcase A "$CB" ""                        $1 $2 $3 $4
  runcase C "$CJ" ""                        $1 $2 $3 $4
  runcase B "$CJ" MTG_DRAGON_PAYOFF_PRUNE=1 $1 $2 $3 $4
done

python3 - "$OUT" <<'PY'
import sys,glob,os
OUT=sys.argv[1]
def load(p):
    d={}
    for line in open(p):
        a=line.split()
        if len(a)>=2: d[int(a[0])]= 9 if int(a[1])==-1 else int(a[1])
    return d
cases=[("0","2002"),("3","2002"),("3","3003"),("5","2002"),("5","3003")]
print(f"{'case':14s} {'A(GT)':>7s} {'C(fix)':>7s} {'B(prune)':>9s}  {'C-A':>7s} {'B-A':>7s}  {'wallC':>6s} {'wallB':>6s}")
tot={'A':0,'C':0,'B':0,'n':0}
for d,s in cases:
    name=f"dragonstorm_regression_d{d}_s{s}"
    r={}
    for cond in "ACB":
        wf=f"{OUT}/{cond}_d{d}_s{s}/wins/{name}.wins"
        r[cond]=load(wf) if os.path.exists(wf) else {}
    keys=sorted(set(r['A'])&set(r['C'])&set(r['B']))
    if not keys: print(f"d{d}_s{s}: MISSING"); continue
    a=sum(r['A'][k] for k in keys)/len(keys)
    c=sum(r['C'][k] for k in keys)/len(keys)
    b=sum(r['B'][k] for k in keys)/len(keys)
    def wall(cond):
        f=f"{OUT}/{cond}_d{d}_s{s}/wall.txt"
        return float(open(f).read()) if os.path.exists(f) else 0
    print(f"d{d}_s{s:5s}     {a:7.4f} {c:7.4f} {b:9.4f}  {c-a:+7.4f} {b-a:+7.4f}  {wall('C'):6.1f} {wall('B'):6.1f}")
    w=len(keys); tot['A']+=a*w; tot['C']+=c*w; tot['B']+=b*w; tot['n']+=w
n=tot['n']
print(f"\nWEIGHTED MEAN  A={tot['A']/n:.4f}  C(fix,noprune)={tot['C']/n:.4f} ({(tot['C']-tot['A'])/n:+.4f})  B(fix,prune)={tot['B']/n:.4f} ({(tot['B']-tot['A'])/n:+.4f})")
PY
echo "AB_DONE"
