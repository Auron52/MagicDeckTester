#!/bin/bash
# Held-out validation of the Dragonstorm payoff-prune: C (fix, no prune) vs B (fix, prune) on the
# OVERNIGHT seeds (4004/5005/6006/7007), which were NOT used while developing the prune. Same binary,
# same (Unclaimed-fix) cards; only MTG_DRAGON_PAYOFF_PRUNE differs. Reports avg turn-to-go-off
# (unwon = 9) per case + weighted mean. If B < C on unseen seeds, the win generalises.
set -e
cd /workspaces/MagicDeckTester2
DECK=decks/Dragonstorm/Dragonstorm.cod; PROF=decks/Dragonstorm/Dragonstorm.profile.json; CJ=src/cards/data/cards.json
OUT=logs/dragonstorm_payoffprune_heldout; rm -rf $OUT; mkdir -p $OUT
CASES=(
 "0 4004 2000 0" "0 5005 2000 0" "0 6006 2000 0" "0 7007 2000 0"
 "3 4004 500 10" "3 5005 500 10" "3 6006 500 10" "3 7007 500 10"
 "5 4004 300 20" "5 5005 300 20" "5 6006 300 20" "5 7007 300 20"
)
runcase() { local cond=$1 env=$2 d=$3 s=$4 g=$5 b=$6
  local name="dragonstorm_overnight_d${d}_s${s}"; local dir="$OUT/${cond}_d${d}_s${s}"; mkdir -p "$dir"
  local JOB
  if [ "$d" -eq 5 ]; then
    JOB="{ \"name\":\"$name\",\"deck\":\"$DECK\",\"profile\":\"$PROF\",\"games\":$g,\"seed\":$s,\"budget_ms\":$b,\"weight\":0 }"
  else
    JOB="{ \"name\":\"$name\",\"deck\":\"$DECK\",\"profile\":\"$PROF\",\"games\":$g,\"seed\":$s,\"depth\":$d,\"budget_ms\":$b,\"ignore_play_profile\":true,\"weight\":0 }"
  fi
  echo "{ \"jobs\": [ $JOB ] }" > "$dir/m.json"
  env $env ./build/Release/mtg --batch "$dir/m.json" --threads 12 --cards-json "$CJ" --game-log-dir "$dir/wins" >"$dir/out.txt" 2>&1
}
for c in "${CASES[@]}"; do set -- $c
  runcase C ""                        $1 $2 $3 $4
  runcase B MTG_DRAGON_PAYOFF_PRUNE=1 $1 $2 $3 $4
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
cases=[(d,s) for d in ("0","3","5") for s in ("4004","5005","6006","7007")]
print(f"{'case':16s} {'C(noprune)':>11s} {'B(prune)':>9s} {'B-C':>8s}")
acc={'C':0,'B':0,'n':0}; accd={}
for d,s in cases:
    name=f"dragonstorm_overnight_d{d}_s{s}"
    C=load(f"{OUT}/C_d{d}_s{s}/wins/{name}.wins") if os.path.exists(f"{OUT}/C_d{d}_s{s}/wins/{name}.wins") else {}
    B=load(f"{OUT}/B_d{d}_s{s}/wins/{name}.wins") if os.path.exists(f"{OUT}/B_d{d}_s{s}/wins/{name}.wins") else {}
    keys=sorted(set(C)&set(B))
    if not keys: print(f"d{d}_s{s}: MISSING"); continue
    c=sum(C[k] for k in keys)/len(keys); b=sum(B[k] for k in keys)/len(keys)
    print(f"d{d}_s{s:5s}       {c:11.4f} {b:9.4f} {b-c:+8.4f}")
    w=len(keys); acc['C']+=c*w; acc['B']+=b*w; acc['n']+=w
    accd.setdefault(d,[0,0,0]); accd[d][0]+=c*w; accd[d][1]+=b*w; accd[d][2]+=w
n=acc['n']
print(f"\nBY DEPTH:")
for d in ("0","3","5"):
    cc,bb,ww=accd[d]; print(f"  d{d}: C={cc/ww:.4f} B={bb/ww:.4f}  B-C={ (bb-cc)/ww:+.4f}")
print(f"\nHELD-OUT MEAN: C(noprune)={acc['C']/n:.4f}  B(prune)={acc['B']/n:.4f}  B-C={(acc['B']-acc['C'])/n:+.4f}")
PY
echo "HELDOUT_DONE"
