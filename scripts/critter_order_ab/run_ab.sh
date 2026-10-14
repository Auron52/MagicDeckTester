#!/usr/bin/env bash
# Build both manifests and run the two pooled batches (base binary, new binary) concurrently.
# Usage: run_ab.sh <threads-per-arm>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
T=${1:-6}
python3 -I "$WT/scripts/critter_order_ab/make_ab.py" build --out "$S/ab_base" --arm base
python3 -I "$WT/scripts/critter_order_ab/make_ab.py" build --out "$S/ab_new" --arm new --hatch
run() {
  local bin=$1 dir=$2
  echo "start $(date -u +%FT%TZ) bin=$bin threads=$T" > "$dir/run.info"
  env -C "$WT" MTG_DUMP_WINS=1 nice -n 10 "$bin" --batch "$dir/manifest.json" --threads "$T" > "$dir/batch.out" 2> "$dir/batch.err"
  echo "exit $? end $(date -u +%FT%TZ)" >> "$dir/run.info"
}
run "$S/bin/mtg.base" "$S/ab_base" &
P1=$!
run "$S/bin/mtg.new" "$S/ab_new" &
P2=$!
wait $P1 $P2
echo "both done $(date -u +%FT%TZ)"
