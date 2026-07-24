#!/usr/bin/env bash
# Definitive Dragonstorm exhaustive mulligan keep+bottom profile generation (continuous, journaled,
# RESUMABLE, poolable). Re-running this SAME command resumes from the per-cell journal.
#
# Config rationale (see .claude/skills/mulligan-profile.md + memory):
#   * d3/b10 rollout config -- the deck-specific SPEED lever (~2x vs d5/b20); "give up a little quality
#     to bring down cost" for this expensive (K=17) profile. (Ideal PLAY uses deeper settings; the
#     mulligan-gen settings are deliberately cheaper -- to be encoded in the play profile later.)
#   * FORCE_MERGE Karrthus+Kolaghan -> K=17 (C(23,7)=245,157 cells, ~29% fewer than natural K=18). Both
#     are hard-to-cast off-red haste-dragon payoffs; merging them is the established Dragonstorm config.
#   * R=40 cap, r0=2 floor, MAXMULL=3, bottoming always on.
#   * Frozen commit baa53d4 (adds the storm go-off short-circuit 324896d + Lotus accel-prefix collapse).
#     Fresh discovery cache on this commit (the tractability cuts change play, so old caches are stale).
#   * TRACTABILITY CUTS: the go-off short-circuit ships default-on; MTG_LOTUS_PREFIX=1 turns on the Lotus
#     collapse as a GEN COST-LEVER (kept opt-in for normal play). Measured on the captured slow rollouts:
#     Lotus+Dragonstorm hands 136s->3s (~46x), Apex hands 640s->131s (~4.9x). This IS the mulligan-gen-vs-
#     ideal-play "give up a little quality to bring down cost" difference the user wanted encoded here.
#   * Continuous pool w/ floor speculation + sub-table fusion (43f3f2b, 30fee8b) + per-cell journal.
set -uo pipefail
cd /workspaces/MagicDeckTester2

HASH=$(git rev-parse --short HEAD)             # f2a56b1 expected
SEED_BASE=${SEED_BASE:-10000001}              # primary-machine rollout run id (1xxxxxxx prefix)
DECK=decks/Dragonstorm/Dragonstorm.cod
GENDIR=logs/Dragonstorm_gen
mkdir -p "$GENDIR"
RAW=decks/Dragonstorm/Dragonstorm.keepmodel.exhaustive.raw.json
PROF=decks/Dragonstorm/Dragonstorm.keepmodel.exhaustive.profile.json
CACHE="$GENDIR/equiv_cache_${HASH}.json"      # fresh cache on the frozen commit

echo "[dragonstorm-keepgen] commit=$HASH seed_base=$SEED_BASE  (resumable: re-run to continue)"
echo "[dragonstorm-keepgen] RAW=$RAW"
echo "[dragonstorm-keepgen] log=$GENDIR/gen_${SEED_BASE}.log"

exec env \
  MTG_LOTUS_PREFIX=1 \
  MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_CONTINUOUS=1 \
  MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=3 MTG_EQUIV_BUDGET=10 \
  MTG_EQUIV_SEED=20260701 \
  MTG_EQUIV_FORCE_MERGE="Karrthus,Dragonlord Kolaghan" \
  MTG_EQUIV_CACHE="$CACHE" \
  MTG_KEEP_ROLLOUTS=40 MTG_KEEP_R_FLOOR=2 MTG_KEEP_MAXMULL=3 \
  MTG_COMMIT="${HASH}+lotusprefix" \
  MTG_KEEP_OUT_RAW="$RAW" MTG_KEEP_OUT_PROFILE="$PROF" \
  MTG_KEEP_SLOW_MS=30000 MTG_KEEP_SLOW_LOG="$GENDIR/slow_captures_${HASH}.txt" \
  ./build/Release/mtg-analyze "$DECK" --cards-json src/cards/data/cards.json \
    --max-turns 8 --seed "$SEED_BASE" --threads 0
