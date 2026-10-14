#!/usr/bin/env bash
# Replay the CritterLifegain references on the base and the new binary (strict).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
mkdir -p "$S/refs"
for arm in base new; do
  env -C "$WT" MTG_BIN="$S/bin/mtg.$arm" python3 test/viewer_protocol_check.py --strict --only CritterLifegain --threads 4 > "$S/refs/$arm.txt" 2>&1
  echo "$arm exit $?" >> "$S/refs/$arm.txt"
done
tail -25 "$S/refs/base.txt"
echo =====
tail -25 "$S/refs/new.txt"
