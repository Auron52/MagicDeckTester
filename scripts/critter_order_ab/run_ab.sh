#!/usr/bin/env bash
# DAY-1 shape of the A/B (make_ab.py: one manifest per binary). Superseded for the resume by run_ab2.sh, which pools
# both arms in ONE batch. Kept runnable, now through the box-wide batch lock (RULES.txt, 2026-10-15): the two
# binaries' batches run one after the other at --threads 20, never side by side.
# Usage: run_ab.sh
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
LOCK=/workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh
python3 -I "$WT/scripts/critter_order_ab/make_ab.py" build --out "$S/ab_base" --arm base
python3 -I "$WT/scripts/critter_order_ab/make_ab.py" build --out "$S/ab_new" --arm new --hatch
run() {
  local bin=$1 dir=$2
  echo "queued $(date -u +%FT%TZ) bin=$bin threads=20" > "$dir/run.info"
  env -C "$WT" MTG_DUMP_WINS=1 bash "$LOCK" batch "$bin" --batch "$dir/manifest.json" --threads 20 > "$dir/batch.out" 2> "$dir/batch.err"
  echo "exit $? end $(date -u +%FT%TZ)" >> "$dir/run.info"
}
run "$S/bin/mtg.base" "$S/ab_base"
run "$S/bin/mtg.new" "$S/ab_new"
echo "both done $(date -u +%FT%TZ)"
