#!/usr/bin/env bash
# dbgrun.sh <bin> <tag> <events file> [ENV=VAL...]: event games (seed:turn) with the MTG_DS2_PAYDBG trace and the
# oracle on the event turn, two worker loops (run under scripts/box_lane.sh --slots 2). Outputs
# $S/rep_<tag>_<seed>{.out,.err,/}; DONE marker $S/rep_<tag>.DONE.
S=/workspaces/MagicDeckTester/build/scratch/ds-apply4
B=$1; tag=$2; ev=$3; shift 3
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply4
one() { local st=$1; shift; local s=${st%:*} t=${st#*:}; local gi=$((s-30000000)) d=$S/rep_${tag}_${st%:*}; rm -rf $d; mkdir -p $d
  env "$@" MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_TURN=$t MTG_DS2_PAYDBG=$t $B decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json --seed $s --game-index $gi --games 1 --log-dir $d > $d.out 2> $d.err; }
mapfile -t E < <(tr ' ' '\n' < $ev | grep .)
worker() { local w=$1; shift; for ((i=w; i<${#E[@]}; i+=2)); do one "${E[$i]}" "$@"; done; }
worker 0 "$@" & worker 1 "$@" & wait
echo done > $S/rep_${tag}.DONE
