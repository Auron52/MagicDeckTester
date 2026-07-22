#!/bin/bash
# OFF-vs-ON A/B for the ritual-payoff guard on ANY deck. Same binary + cards.json both variants.
# args: deckkey deckfile profile depth seed games budget
set -e
K=$1; DECK=$2; PROF=$3; D=$4; S=$5; G=$6; B=$7
DIR=logs/dragonstorm_ritual_ab/${K}_d${D}_s${S}
mkdir -p "$DIR"
name="${K}_d${D}_s${S}"
# d5: let the profile own depth/bottoming (value model); d0/d3: pin depth + ignore_play_profile.
if [ "$D" -eq 5 ]; then
  JOB="{ \"name\": \"$name\", \"deck\": \"$DECK\", \"profile\": \"$PROF\", \"games\": $G, \"seed\": $S, \"budget_ms\": $B, \"weight\": 0 }"
else
  JOB="{ \"name\": \"$name\", \"deck\": \"$DECK\", \"profile\": \"$PROF\", \"games\": $G, \"seed\": $S, \"depth\": $D, \"budget_ms\": $B, \"ignore_play_profile\": true, \"weight\": 0 }"
fi
echo "{ \"jobs\": [ $JOB ] }" > "$DIR/manifest.json"
for variant in OFF ON; do
  rm -rf "$DIR/${variant}_wins"
  ENV=""; [ "$variant" = "ON" ] && ENV="MTG_RITUAL_PAYOFF_GUARD=1"
  env $ENV ./build/Release/mtg --batch "$DIR/manifest.json" --threads 12 \
     --game-log-dir "$DIR/${variant}_wins" 2>"$DIR/${variant}.err" | sed "s/^/  [$variant] /"
done
python3 - "$DIR" "$name" <<'PY'
import sys
DIR,name=sys.argv[1],sys.argv[2]
def load(v):
    d={}
    for line in open(f"{DIR}/{v}_wins/{name}.wins"):
        gi,wt,dg=line.split(); wt=int(wt); d[int(gi)]=wt if wt>0 else 9
    return d
OFF=load("OFF"); ON=load("ON"); gis=sorted(OFF)
aoff=sum(OFF[g] for g in gis)/len(gis); aon=sum(ON[g] for g in gis)/len(gis)
faster=sum(1 for g in gis if ON[g]<OFF[g]); slower=sum(1 for g in gis if ON[g]>OFF[g])
print(f"  {name}: n={len(gis)} avg_turns OFF={aoff:.4f} ON={aon:.4f} speedup={aoff-aon:+.4f} (pos=faster) faster={faster} slower={slower}")
PY
