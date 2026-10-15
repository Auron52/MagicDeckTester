#!/usr/bin/env bash
# Stage-1 recovery of F1-F4's two slower games (Hinata2 shipped gi 79 off T4 / F1-F4 T5, gi 85 off T6 / T7):
# --depth W --budget-ms 100, W = the faster arm's turn, the F1-F4 arm (F5 off = the shipped default), one at a
# time (--threads 1, niced; RULES item 4).
set -u
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/scripts/bp_open
H="decks/Hinata2/Hinata2"
bash "$S/one_game.sh" hin79_f14_s1 "$H.cod" "$H.profile.json" 10560080 79 --depth 4 --budget-ms 100 --ignore-play-profile -- MTG_BP_NEW_ONLY_DEMOTE=0
bash "$S/one_game.sh" hin85_f14_s1 "$H.cod" "$H.profile.json" 10560086 85 --depth 6 --budget-ms 100 --ignore-play-profile -- MTG_BP_NEW_ONLY_DEMOTE=0
grep -H "\[play\]\|\[win\]" /workspaces/MagicDeckTester/build/scratch/bp_open/one/hin*_s1/err.txt
