#!/usr/bin/env bash
# run_batch.sh <bin> <outdir> [extra env assignments...] -- runs <outdir>/manifest.json, 4 threads, niced.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
bin="$1"; out="$2"; shift 2
env MTG_PGO=0 MTG_DUMP_WINS=1 "$@" nice -n 19 "$bin" --batch "$out/manifest.json" --threads 4 \
    > "$out/batch.out" 2> "$out/batch.err"
echo "EXIT $?"
tail -3 "$out/batch.out"
