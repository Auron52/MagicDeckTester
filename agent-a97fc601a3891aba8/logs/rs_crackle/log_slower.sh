#!/usr/bin/env bash
# Log both arms (MTG_CRACKLE_COUNT_EXACT=1 / =0) of every A/B game the fix made slower.
# -> logs/rs_crackle/LS/<cell>_<gi>_{on,off}/
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
BIN=$R/mtg_fix1
while read -r cell gi seed; do
  extra=()
  c=$cell
  if [ "$cell" = 2hg ]; then extra=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2); c=d0; fi
  for arm in on off; do
    v=1; [ $arm = off ] && v=0
    echo "$cell gi=$gi $arm: $(bash $R/game.sh $BIN $c $gi $seed $R/LS/${cell}_${gi}_${arm} MTG_CRACKLE_COUNT_EXACT=$v "${extra[@]}")"
  done
done <<'EOF'
d0 8 7900009
d0 34 7900035
d0 157 7900158
d0 196 7900197
d0 212 7900213
d0 234 7900235
d0 260 7900261
d0 324 7900325
d3 69 7910070
ship 36 7920037
2hg 90 7930091
EOF
