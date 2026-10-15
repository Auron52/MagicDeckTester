#!/usr/bin/env bash
# Before the queued v6 batch acquires its slot: point it at the binary with all three engine levers and
# the final arm set, keeping the FIFO ticket. Binary first (a new binary runs the old manifest fine; the
# old binary would reject the new manifest's lever names), each by an atomic rename.
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
D=$SC/ab_v6
if grep -q "slot acquired\|batch slot" "$D/batch.err" && ! grep -q "queued" "$D/batch.err"; then echo "already running"; exit 1; fi
if [ -s "$D/batch.out" ]; then echo "already running (batch.out non-empty)"; exit 1; fi
cp "$SC/bin/mtg.svn" "$SC/bin/mtg.svnonly"
cp "$SC/bin/mtg.pf" "$SC/bin/mtg.svn.new"
mv "$SC/bin/mtg.svn.new" "$SC/bin/mtg.svn"
mkdir -p "$SC/ab_v6new"
python3 "$WT/test/tools/stompy_order_ab/gen_v6.py" "$SC/ab_v6new"
mv "$SC/ab_v6new/manifest.json" "$D/manifest.json"
date -u +"%FT%TZ swapped: bin mtg.svn := mtg.pf (svn + pd + pf levers; old one kept as mtg.svnonly), manifest := gen_v6.py final arms" > "$D/SWAP.txt"
cat "$D/SWAP.txt"
cmp "$SC/bin/mtg.svn" "$SC/bin/mtg.pf" && echo "binary swapped"
