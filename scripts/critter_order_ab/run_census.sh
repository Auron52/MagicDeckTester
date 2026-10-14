#!/usr/bin/env bash
# Heliod placement-case / flip census on executor mains (diag binary, play-identical to mtg.new).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
T=${1:-3}
python3 -I "$WT/scripts/critter_order_ab/heliod_census.py" build "$S/census"
echo "start $(date -u +%FT%TZ)" > "$S/census/run.info"
env -C "$WT" MTG_DUMP_WINS=1 MTG_USER_ORDER_TRACE=1 nice -n 10 "$S/bin/mtg.diag" --batch "$S/census/manifest.json" --threads "$T" > "$S/census/batch.out" 2> "$S/census/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$S/census/run.info"
python3 -I "$WT/scripts/critter_order_ab/heliod_census.py" count "$S/census"
