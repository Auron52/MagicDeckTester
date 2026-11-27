#!/bin/bash
# gdiff.sh <deck_dir> <gi> "<armA env>" "<armB env>" [extra mtg args]: plays one game per arm with logs, prints
# each arm's per-turn actions and the first turn/phase where they differ. BASE (default 7460000), BIN (mtg.ab2),
# DEPTH (default d0 args) env.
D=$1; G=$2; A=$3; B=$4; shift 4
SC=/workspaces/MagicDeckTester/build/scratch/elves-lordcast; BIN=${BIN:-$SC/mtg.ab2}; BASE=${BASE:-7460000}
N=$(basename "$D"); DECK=$(ls "$D"/"$N".cod "$D"/"$N".txt 2>/dev/null | head -1)
ARGS=${PLAYARGS:-"--ignore-play-profile --depth 0 --budget-ms 0"}
O=$SC/gd/${N}_${G}; rm -rf $O; mkdir -p $O/a $O/b
env MTG_RSS_CAP_GB=${CAP:-6} $A $BIN "$DECK" --profile "$D/$N.profile.json" $ARGS --games 1 --seed $((BASE+G)) --game-index $G --log-dir $O/a "$@" > $O/a.out 2> $O/a.err
env MTG_RSS_CAP_GB=${CAP:-6} $B $BIN "$DECK" --profile "$D/$N.profile.json" $ARGS --games 1 --seed $((BASE+G)) --game-index $G --log-dir $O/b "$@" > $O/b.out 2> $O/b.err
python3 - $O <<'PY'
import json,glob,sys
O=sys.argv[1]
def load(x):
    f=glob.glob(f"{O}/{x}/*.json"); 
    if not f: return None
    g=json.load(open(f[0])); rows=[]
    for t in g['turns']:
        acts=[]
        for a in t['actions']:
            s=a.get('description') or a.get('cardName') or a.get('type')
            acts.append(str(s))
        rows.append((t['turn'],t['phase'],acts))
    return g,rows
for x in 'ab':
    r=load(x); print(x, 'win turn', r[0].get('winTurn', r[0].get('result')) if r else None)
ga,ra=load('a'); gb,rb=load('b')
for (ta,pa,aa),(tb,pb,ab) in zip(ra,rb):
    if (ta,pa,aa)!=(tb,pb,ab):
        print(f"FIRST DIFF T{ta} {pa}\n  A: {aa}\n  B: {ab}"); break
PY
