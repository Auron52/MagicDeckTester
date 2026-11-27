#!/usr/bin/env bash
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash /workspaces/MagicDeckTester/.claude/worktrees/ds-apply3/logs/ds-apply3/run_gate.sh /workspaces/MagicDeckTester/build/scratch/ds-apply3/bin_l3/mtg /workspaces/MagicDeckTester/build/scratch/ds-apply3/gate.json /workspaces/MagicDeckTester/build/scratch/ds-apply3/gate_out
date -u +%FT%TZ > /workspaces/MagicDeckTester/build/scratch/ds-apply3/chain.gate_end
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash /workspaces/MagicDeckTester/.claude/worktrees/ds-apply3/logs/ds-apply3/run_ab.sh /workspaces/MagicDeckTester/build/scratch/ds-apply3/bin_l3/mtg /workspaces/MagicDeckTester/build/scratch/ds-apply3/ab.json /workspaces/MagicDeckTester/build/scratch/ds-apply3/ab_out
date -u +%FT%TZ > /workspaces/MagicDeckTester/build/scratch/ds-apply3/chain.ab_end
echo done > /workspaces/MagicDeckTester/build/scratch/ds-apply3/chain.DONE
