#!/bin/bash
# OFF-vs-ON A/B for the d0/rollout rituals-for-payoff guard (MTG_RITUAL_PAYOFF_GUARD).
# Same working-tree binary + same cards.json for both variants -> isolates the guard.
# args: depth seed games budget
set -e
D=$1; S=$2; G=$3; B=$4
DIR=logs/dragonstorm_ritual_ab/case_d${D}_s${S}
mkdir -p "$DIR"
name="dragonstorm_d${D}_s${S}"
if [ "$D" -eq 5 ]; then
  JOB="{ \"name\": \"$name\", \"deck\": \"decks/Dragonstorm/Dragonstorm.cod\", \"profile\": \"decks/Dragonstorm/Dragonstorm.profile.json\", \"games\": $G, \"seed\": $S, \"budget_ms\": $B, \"weight\": 0 }"
else
  JOB="{ \"name\": \"$name\", \"deck\": \"decks/Dragonstorm/Dragonstorm.cod\", \"profile\": \"decks/Dragonstorm/Dragonstorm.profile.json\", \"games\": $G, \"seed\": $S, \"depth\": $D, \"budget_ms\": $B, \"ignore_play_profile\": true, \"weight\": 0 }"
fi
echo "{ \"jobs\": [ $JOB ] }" > "$DIR/manifest.json"
for variant in OFF ON; do
  rm -rf "$DIR/${variant}_wins"
  ENV=""
  if [ "$variant" = "ON" ]; then ENV="MTG_RITUAL_PAYOFF_GUARD=1"; fi
  env $ENV ./build/Release/mtg --batch "$DIR/manifest.json" --threads 12 \
     --game-log-dir "$DIR/${variant}_wins" 2>"$DIR/${variant}.err" \
     | sed "s/^/  [$variant] /"
done
python3 - "$DIR" "$name" <<'PY'
import sys
DIR,name=sys.argv[1],sys.argv[2]
def load(v):
    d={}
    for line in open(f"{DIR}/{v}_wins/{name}.wins"):
        gi,wt,dg=line.split(); wt=int(wt); d[int(gi)]=wt if wt>0 else 9
    return d
OFF=load("OFF"); ON=load("ON")
gis=sorted(OFF)
faster=[(gi,OFF[gi],ON[gi]) for gi in gis if ON[gi]<OFF[gi]]
slower=[(gi,OFF[gi],ON[gi]) for gi in gis if ON[gi]>OFF[gi]]
aoff=sum(OFF[gi] for gi in gis)/len(gis)
aon =sum(ON[gi]  for gi in gis)/len(gis)
print(f"  n={len(gis)}  avg_turns OFF={aoff:.4f}  ON={aon:.4f}  speedup={aoff-aon:+.4f} turns  (pos=faster)")
print(f"  games faster={len(faster)}  slower={len(slower)}")
for gi,a,b in faster[:15]: print(f"    faster gi{gi}: turn {a} -> {b}")
for gi,a,b in slower[:15]: print(f"    slower gi{gi}: turn {a} -> {b}")
PY
