#!/usr/bin/env bash
# evrun.sh <bin> <tag> [ENV=VAL...]: every event game (events.txt), oracle on the event turn; DONE marker rep_<tag>.DONE
S=/workspaces/MagicDeckTester/build/scratch/ds-apply3
B=$1; tag=$2; shift 2
REPO=/workspaces/MagicDeckTester/.claude/worktrees/ds-apply3 $S/rep.sh $B $tag $(cat $S/events.txt) -- "$@" MTG_X_DUMMY=0
