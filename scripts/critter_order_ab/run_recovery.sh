#!/usr/bin/env bash
# Stage-1 / stage-2 recovery of every slower game of the resumed A/B (moved.json from make_ab2.py analyse),
# ONE pooled batch on mtg.new through the box-wide lock.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
LOCK=/workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh
python3 -I "$WT/scripts/critter_order_ab/recovery.py" build "$S/ab2_A" "$S/recovery"
echo "queued $(date -u +%FT%TZ)" > "$S/recovery/run.info"
env -C "$WT" MTG_DUMP_WINS=1 bash "$LOCK" batch "$S/bin/mtg.new" --batch "$S/recovery/manifest.json" --threads 20 > "$S/recovery/batch.out" 2> "$S/recovery/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$S/recovery/run.info"
python3 -I "$WT/scripts/critter_order_ab/recovery.py" analyse "$S/ab2_A" "$S/recovery"
