#!/usr/bin/env bash
# stagetests.sh -- each commit's OWN new tests, run on the final mtg-test with every LATER commit's
# levers at 0 (a lever at 0 restores the earlier behaviour exactly, so this is that commit's tree's
# behaviour). Output: $S/stagetests.log
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
S=/workspaces/MagicDeckTester/build/scratch/agent-a91b77d62216a1d12
out=$S/stagetests.log
: > "$out"
run() {   # run <stage> <tc filter> [ENV=0 ...]
  local st=$1 tc=$2; shift 2
  echo "=== stage $st  tc=$tc  env=$*" >> "$out"
  (cd "$W" && env "$@" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$W/build/Release/mtg-test" -tc="$tc" >> "$out" 2>&1)
  echo "stage $st exit=$?" >> "$out"
}
H=MTG_SOLVE_ATTACK_ONLY_WIN=0
G="MTG_ATTACK_TAP_ABILITY_KINDS=0 MTG_ATTACK_TAP_PAYER_ORDER=0"
F="MTG_ATTACK_AURA_HOST_TAP=0 MTG_ATTACK_TAP_CREATURE_ONLY=0"
E=MTG_LAND_BOUNCE_GUARD=0
D=MTG_BRUNA_LETHAL_SHORTCIRCUIT=0
C="MTG_ATTACK_TAP_REPROJECT=0 MTG_ATTACK_TEAM_PUMP_ETB=0 MTG_ATTACK_PLAN_LIFEGAIN=0"
B=MTG_ATTACK_CANNONS_TRIGGER=0
TA="F1*,F6*"
TB="$TA,F2*"
TC="$TB,F3*,F4a*,F4b*"
TD="$TC,F5.1*"
TE="$TD,F5.4*"
TF="$TE,F5.2*,F5.3*"
TG="$TF,Point 8*,Point 9*"
TH="$TG,Solve attack-only*"
run A "$TA" $B $C $D $E $F $G $H
run B "$TB" $C $D $E $F $G $H
run C "$TC" $D $E $F $G $H
run D "$TD" $E $F $G $H
run E "$TE" $F $G $H
run F "$TF" $G $H
run G "$TG" $H
run H "$TH" MTG_UNUSED_PLACEHOLDER=1
echo ALLDONE >> "$out"
