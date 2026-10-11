#!/usr/bin/env bash
# vbatch.sh <bin-on-volume> <dir-on-volume> [ENV=VAL ...] -- one pooled `mtg --batch <dir>/manifest.json`
# under lowprio.sh (cores 0-5, nice 19), --threads 6, MTG_DUMP_WINS=1 for paired analysis.
# Output: <dir>/batch.out, <dir>/batch.err (both on the build volume). Prints the digests file path.
bin="$1"; dir="$2"; shift 2
cd /workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6 || exit 1
env MTG_PGO=0 MTG_DUMP_WINS=1 "$@" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$bin" \
    --batch "$dir/manifest.json" --threads 6 > "$dir/batch.out" 2> "$dir/batch.err"
rc=$?
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$dir/batch.out" | sort > "$dir/digests.txt"
echo "EXIT $rc jobs=$(wc -l < "$dir/digests.txt")"
