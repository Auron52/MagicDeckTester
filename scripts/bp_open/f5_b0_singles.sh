#!/usr/bin/env bash
# F5's slower games replayed with all five levers at an UNLIMITED budget, their own cell's depth, one at a
# time (--threads 1, niced; RULES item 4). Mirrorwing shipped gi 141 was run by hand (T4 = off's turn).
set -u
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/scripts/bp_open
CG="decks/Creature Giving/Creature Giving"
bash "$S/one_game.sh" cg196_on_b0 "$CG.cod" "$CG.profile.json" 10640197 196 --budget-ms 0 -- MTG_BP_NEW_ONLY_DEMOTE=1
bash "$S/one_game.sh" cg298_on_b0 "$CG.cod" "$CG.profile.json" 10645299 298 --depth 3 --budget-ms 0 --ignore-play-profile -- MTG_BP_NEW_ONLY_DEMOTE=1
bash "$S/one_game.sh" bruna5_on_b0 "decks/Bruna/Bruna.cod" "decks/Bruna/Bruna.profile.json" 11045006 5 --depth 3 --budget-ms 0 --ignore-play-profile -- MTG_BP_NEW_ONLY_DEMOTE=1
grep -H "\[play\]\|\[win\]" /workspaces/MagicDeckTester/build/scratch/bp_open/one/*_b0/err.txt
