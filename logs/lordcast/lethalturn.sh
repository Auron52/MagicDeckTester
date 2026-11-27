#!/bin/bash
# lethalturn.sh <deck_dir> <gi> <turn>: on-arm d0 log; prints that turn's main-1 casts, newly tapped creatures and the attack.
D=$1; G=$2; T=$3; N=$(basename "$D"); DECK=$(ls "$D"/"$N".cod "$D"/"$N".txt 2>/dev/null | head -1)
S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast/rc/lt_${N}_$G; rm -rf $S; mkdir -p $S
MTG_RSS_CAP_GB=6 MTG_ATTACK_LORD_CAST=1 /workspaces/MagicDeckTester/build/scratch/elves-lordcast/mtg.ab "$DECK" --profile "$D/$N.profile.json" \
  --ignore-play-profile --depth 0 --budget-ms 0 --games 1 --seed $((7340000+G)) --game-index $G --log-dir $S > /dev/null 2>&1
python3 - "$S" "$T" <<'PY'
import json,glob,sys
g=json.load(open(glob.glob(sys.argv[1]+"/*.json")[0])); T=int(sys.argv[2]); prev=None
for t in g['turns']:
    if t['turn']!=T: continue
    bf={p['card']:(p['cardName'],p['tapped'],p['isLand']) for p in t['boardAfter']['battlefield']}
    if t['phase']=='DRAW': prev=bf
    if t['phase']=='MAIN_1':
        casts=[a.get('cardName') for a in t['actions']]
        newtap=[v[0] for k,v in bf.items() if v[1] and not v[2] and k in (prev or {}) and not prev[k][1]]
        lands_open=[v[0] for v in bf.values() if v[2] and not v[1]]
        print(f"gi{sys.argv[1].split('_')[-1]} T{T} casts={casts} creatures_tapped={newtap} lands_left_untapped={lands_open}", end=" ")
    if t['phase']=='COMBAT':
        print("attack:",[(a.get('damage'),a.get('oppLife')) for a in t['actions']])
PY
