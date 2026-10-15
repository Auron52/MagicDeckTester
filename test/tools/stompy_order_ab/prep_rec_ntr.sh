#!/usr/bin/env bash
# Recovery repro manifests: the 2HG games unrecovered under ofix / onotop, re-run with the target-rank lever
# (ntr) and the same-name victim lever (svn); the payment fix's two new v5 movers (fix alone).
# usage: prep_rec_ntr.sh
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
T=$WT/test/tools/stompy_order_ab
D=$SC/rec_ntr
mkdir -p "$D/wins"
printf 'stompy2hg ship 160 4 5\nstompy2hg ship 797 4 5\nstompy2hg d3 797 4 5\nstompy2hg d3 2290 4 5\nstompy2hg d3 2791 4 5\n' > "$D/slower.2hg.txt"
grep -E '^(selesnya d3 964|fivecolour d0 1040) ' "$SC/ab_v5/slower.movers.txt" > "$D/slower.movers.txt"
cat "$D/slower.movers.txt"
python3 "$T/gen_rec.py" "$D/m2hg.json" ofixntr,ofixsvn,onotopsvn "$D/slower.2hg.txt"
python3 "$T/gen_rec.py" "$D/mmov.json" fix "$D/slower.movers.txt"
