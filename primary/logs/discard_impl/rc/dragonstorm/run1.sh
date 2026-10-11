#!/bin/bash
# Repro dragonstorm_smoke_d0_s1001 gi533: flag on vs off.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/dragonstorm
python3 $R/test/explain_game.py smoke dragonstorm_smoke_d0_s1001 533 > $O/explain.txt 2>&1
for arm in on off; do
  if [ $arm = on ]; then F=true; else F=false; fi
  cat > $O/m_$arm.json <<EOF
{"jobs":[{"name":"x_$arm","deck":"decks/Dragonstorm/Dragonstorm.cod","profile":"decks/Dragonstorm/Dragonstorm.profile.json","games":1,"seed":1534,"game_index":533,"depth":0,"budget_ms":0,"ignore_play_profile":true,"weight":0,"flags":{"MTG_DRAGONSTORM_BUCKET_DISCARD":$F}}]}
EOF
  mkdir -p $O/gl_$arm
  (cd $R; MTG_DUMP_WINS=1 MTG_TRACE=discard nice -n 19 $R/build/PGO/mtg --batch $O/m_$arm.json --threads 1 --game-log-dir $O/gl_$arm > $O/out_$arm.txt 2> $O/err_$arm.txt)
done
