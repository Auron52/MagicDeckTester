#!/usr/bin/env bash
# rep.sh <bin> <tag> <seed:turn>... [-- ENV=VAL...]: single event games, oracle on the event turn, game logs kept.
B=$1; tag=$2; shift 2; envs=()
args=(); while [ $# -gt 0 ]; do if [ "$1" = "--" ]; then shift; envs=("$@"); break; fi; args+=("$1"); shift; done
cd ${REPO:-/workspaces/MagicDeckTester/.claude/worktrees/ds-apply4}
S=/workspaces/MagicDeckTester/build/scratch/ds-apply4
for st in "${args[@]}"; do s=${st%:*}; t=${st#*:}; gi=$((s-30000000)); d=$S/rep_${tag}_$s; rm -rf $d; mkdir -p $d
  env "${envs[@]}" MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_TURN=$t $B decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json --seed $s --game-index $gi --games 1 --log-dir $d > $d.out 2> $d.err
done
echo done > $S/rep_${tag}.DONE
