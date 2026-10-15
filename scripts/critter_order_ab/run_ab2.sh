#!/usr/bin/env bash
# The resumed CritterLifegain cast-order A/B (make_ab2.py): batch A (both arms pooled), then batch B on mtg.base
# (identity + the =0 check slices), each through the box-wide batch lock at --threads 20 (RULES.txt).
# Batch A runs mtg.diag (fb96134a: af7434cc + the MTG_USER_ORDER_TRACE note, play-identical) with the trace on, so
# the Heliod-case / flip census comes out of the same games (heliod_census.py count <ab2_A>).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
LOCK=/workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh
python3 -I "$WT/scripts/critter_order_ab/make_ab2.py" build A "$S/ab2_A"
python3 -I "$WT/scripts/critter_order_ab/make_ab2.py" build B "$S/ab2_B"
run() {
  local bin=$1 dir=$2 trace=$3
  echo "queued $(date -u +%FT%TZ) bin=$bin trace=$trace" > "$dir/run.info"
  env -C "$WT" MTG_DUMP_WINS=1 MTG_USER_ORDER_TRACE="$trace" bash "$LOCK" batch "$bin" --batch "$dir/manifest.json" --threads 20 > "$dir/batch.out" 2> "$dir/batch.err"
  echo "exit $? end $(date -u +%FT%TZ)" >> "$dir/run.info"
}
run "$S/bin/mtg.diag" "$S/ab2_A" 1
run "$S/bin/mtg.base" "$S/ab2_B" 0
python3 -I "$WT/scripts/critter_order_ab/make_ab2.py" analyse "$S/day1_base" "$S/day1_new" "$S/ab2_A" "$S/ab2_B"
python3 -I "$WT/scripts/critter_order_ab/heliod_census.py" count "$S/ab2_A"
