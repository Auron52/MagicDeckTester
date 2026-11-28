#!/usr/bin/env bash
# chain_big.sh: under the big lane (claude_box.lock, coordinator 2026-11-28 11:07), full threads, MTG_RSS_CAP_GB=38:
# the all-deck held-out A/B (ab.json -> ab_out/), then the gate ON arm (gate_on.json -> gate_on/). Markers in $S.
S=/workspaces/MagicDeckTester/build/scratch/ds-apply5
B=$S/bin_l5c/mtg
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply5
exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock
flock 9
date -u +%FT%TZ > $S/chain.start
export MTG_RSS_CAP_GB=38
bash logs/ds-apply5/run_ab.sh $B $S/ab.json $S/ab_out "$(nproc)"
date -u +%FT%TZ > $S/chain.ab_end
bash logs/ds-apply5/run_gate.sh $B $S/gate_on.json $S/gate_on "$(nproc)"
date -u +%FT%TZ > $S/chain.gate_end
echo done > $S/chain.DONE
