#!/usr/bin/env bash
# unitchain.sh -- single-threaded verification chain, sequential:
#  1. full unit suite, defaults (the final commit)
#  2. full unit suite minus the new file, every new lever =0 (the pre-round behaviour)
#  3. per-stage new tests (stagetests.sh)    4. per-stage -fsyntax-only (stagecheck.sh)
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
S=/workspaces/MagicDeckTester/build/scratch/agent-a91b77d62216a1d12
bash "$W/logs/attack_fix/unit.sh" "$S/unit_final.log"
OFF="MTG_ATTACK_DOMAIN_DORK_YIELD=0 MTG_ATTACK_TAP_ASTROLABE_CREDIT=0 MTG_ATTACK_CANNONS_TRIGGER=0 MTG_ATTACK_TAP_REPROJECT=0 MTG_ATTACK_TEAM_PUMP_ETB=0 MTG_ATTACK_PLAN_LIFEGAIN=0 MTG_BRUNA_LETHAL_SHORTCIRCUIT=0 MTG_LAND_BOUNCE_GUARD=0 MTG_ATTACK_AURA_HOST_TAP=0 MTG_ATTACK_TAP_CREATURE_ONLY=0 MTG_ATTACK_TAP_ABILITY_KINDS=0 MTG_ATTACK_TAP_PAYER_ORDER=0 MTG_SOLVE_ATTACK_ONLY_WIN=0"
env $OFF bash "$W/logs/attack_fix/unit.sh" "$S/unit_alloff.log" -sfe=*test_attack_projection*
bash "$W/logs/attack_fix/stagetests.sh"
bash "$W/logs/attack_fix/stagecheck.sh"
echo CHAINDONE > "$S/unitchain.done"
