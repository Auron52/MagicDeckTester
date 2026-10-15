#!/usr/bin/env bash
# The MTG_CRITTER_HELIOD_FIRST amendment A/B (make_ab3.py): ONE pooled batch, both arms, through the box lock.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
LOCK=/workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh
python3 -I "$WT/scripts/critter_order_ab/make_ab3.py" build "$S/ab3"
echo "queued $(date -u +%FT%TZ)" > "$S/ab3/run.info"
env -C "$WT" MTG_DUMP_WINS=1 MTG_USER_ORDER_TRACE=1 bash "$LOCK" batch "$S/bin/mtg.new2" --batch "$S/ab3/manifest.json" --threads 20 > "$S/ab3/batch.out" 2> "$S/ab3/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$S/ab3/run.info"
python3 -I "$WT/scripts/critter_order_ab/make_ab3.py" analyse "$S/ab3" "$S/ab2_A"
