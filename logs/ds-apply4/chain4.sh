#!/usr/bin/env bash
# ds-apply4 chain: wait for the parent's landing (LANDED), then the gate and the held-out A/B, each under the box lock.
while [ ! -f /workspaces/MagicDeckTester/build/scratch/land-1129/LANDED ]; do sleep 30; done
date -u +%FT%TZ > /workspaces/MagicDeckTester/build/scratch/ds-apply4/chain.start
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash /workspaces/MagicDeckTester/.claude/worktrees/ds-apply4/logs/ds-apply4/run_gate.sh /workspaces/MagicDeckTester/build/scratch/ds-apply4/bin_l4/mtg /workspaces/MagicDeckTester/build/scratch/ds-apply4/gate.json /workspaces/MagicDeckTester/build/scratch/ds-apply4/gate_out
date -u +%FT%TZ > /workspaces/MagicDeckTester/build/scratch/ds-apply4/chain.gate_end
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash /workspaces/MagicDeckTester/.claude/worktrees/ds-apply4/logs/ds-apply4/run_ab.sh /workspaces/MagicDeckTester/build/scratch/ds-apply4/bin_l4/mtg /workspaces/MagicDeckTester/build/scratch/ds-apply4/ab.json /workspaces/MagicDeckTester/build/scratch/ds-apply4/ab_out
date -u +%FT%TZ > /workspaces/MagicDeckTester/build/scratch/ds-apply4/chain.ab_end
echo done > /workspaces/MagicDeckTester/build/scratch/ds-apply4/chain.DONE
