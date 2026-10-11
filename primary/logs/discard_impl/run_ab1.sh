#!/usr/bin/env bash
# Wait for the PGO build (PID $1), then run the ONE pooled discard A/B (logs/discard_impl/ab1).
# Uses build/PGO only when its stamp matches HEAD:src (byte-identical games to Release either way).
set -u
REPO=/workspaces/MagicDeckTester
OUT=$REPO/logs/discard_impl/ab1
BUILD_PID=$1
while kill -0 "$BUILD_PID" 2>/dev/null; do sleep 20; done
BIN=$REPO/build/Release/mtg
if [ "$(cat "$REPO/build/PGO/SRC_TREE" 2>/dev/null)" = "$(git -C "$REPO" rev-parse HEAD:src)" ] \
   && [ -z "$(git -C "$REPO" status --porcelain --untracked-files=no -- src)" ]; then
    BIN=$REPO/build/PGO/mtg
fi
echo "ab1: binary $BIN, HEAD $(git -C "$REPO" rev-parse --short HEAD), start $(date -u +%FT%TZ)" > "$OUT/run.info"
MTG_DUMP_WINS=1 nice -n 10 "$BIN" --batch "$OUT/manifest.json" --threads 20 2> "$OUT/batch.err" > "$OUT/batch.out"
echo "ab1: exit $? end $(date -u +%FT%TZ)" >> "$OUT/run.info"
