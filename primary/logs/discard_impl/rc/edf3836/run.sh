#!/bin/bash
# EDF d0 gi3836 (seed 433837): policy on vs on + MTG_EDF_OUTLET_PIPS, with game logs; print discards.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf3836
for arm in 0 1; do
  mkdir -p $O/log_$arm
  (cd $R; MTG_EDF_BUCKET_DISCARD=1 MTG_EDF_OUTLET_PIPS=$arm nice -n 19 $R/build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json --games 1 --seed 433837 --game-index 3836 --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 --log-dir $O/log_$arm > $O/out_$arm.txt 2> $O/err_$arm.txt)
  echo "pips=$arm: $(grep -o 'avg (turns)   : [0-9.]*' $O/out_$arm.txt)"
done
python3 - "$O" <<'EOF'
import json, glob, sys
O = sys.argv[1]
for arm in (0, 1):
    g = json.load(open(glob.glob('%s/log_%d/*.json' % (O, arm))[0]))
    print('--- pips=%d' % arm)
    for t in g['turns']:
        acts = [a for a in t.get('actions', []) if a.get('type') != 'DISCARD']
        disc = [a['cardName'] for a in t.get('actions', []) if a.get('type') == 'DISCARD']
        names = [a.get('cardName', a.get('type')) for a in acts]
        line = 'T%s %-8s %s' % (t.get('turn'), t.get('phase'), ', '.join(str(x) for x in names)[:150])
        if disc:
            ba = t.get('boardAfter', {})
            line += '  DISCARD %s | hand after %s' % (disc, ba.get('hand'))
        if names or disc:
            print(line)
EOF
