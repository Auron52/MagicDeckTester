#!/usr/bin/env bash
# §2b sweep scaffolding: what rank should a §2a payment source (IsPaySacSource) carry?
#
# Mirrorwing's ManaSourceRank ladder is {10 Forest/Mountain/Gruul Turf/Sandstone Needle/Elvish
# Mystic, 20 Game Trail/Rootbound Crag, 30 Ignoble Hierarch}, and nothing reaches the 60+ reserve
# tiers -- so a Treasure has exactly FOUR distinguishable slots on this deck:
#
#   base  (unset -> 50 by the rainbow fall-through)  = spend LAST of every source
#   25                                               = after the lands, before the Hierarch
#   15                                               = after the monos, before the duals
#   5                                                = spend FIRST, before every land
#
# 45 / 59 / 64 are all indistinguishable from base here, so they are not swept.
# Paired: every arm runs the SAME seeds, so per-game win turns pair directly (common random
# numbers -- roughly 4x the variance reduction of an unpaired read; see §6 of
# docs/design/lump-mana-sources-as-payment-sources.md, where an unpaired read got the sign wrong).
#
# THROWAWAY. Delete with the MTG_PAYSAC_RANK selector once the result is recorded.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${OUT:-logs/paysac_rank}
GAMES_PER_JOB=${GAMES_PER_JOB:-500}
JOBS=${JOBS:-20}
SEED_BASE=${SEED_BASE:-930000}
DEPTH=${DEPTH:-2}
BUDGET=${BUDGET:-3}
ARMS=${ARMS:-"base 25 15 5"}

DECK="decks/Mirrorwing Dragon/Mirrorwing Dragon.cod"
PROF="decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"

mkdir -p "$OUT"
MAN="$OUT/manifest.json"
python3 - "$MAN" "$DECK" "$PROF" "$GAMES_PER_JOB" "$JOBS" "$SEED_BASE" "$DEPTH" "$BUDGET" <<'PY'
import json, sys
man, deck, prof, g, n, base, depth, budget = sys.argv[1:]
g, n, base, depth, budget = int(g), int(n), int(base), int(depth), int(budget)
jobs = [{"name": f"mw_s{base+i*1000}", "deck": deck, "profile": prof,
         "games": g, "seed": base + i * 1000, "depth": depth,
         "budget_ms": budget, "max_turns": 8} for i in range(n)]
json.dump({"jobs": jobs}, open(man, "w"), indent=1)
PY

for ARM in $ARMS; do
  DIR="$OUT/arm_$ARM"
  rm -rf "$DIR"; mkdir -p "$DIR"
  if [ "$ARM" = "base" ]; then unset MTG_PAYSAC_RANK || true; else export MTG_PAYSAC_RANK="$ARM"; fi
  echo "=== arm $ARM (MTG_PAYSAC_RANK=${MTG_PAYSAC_RANK:-unset}) ==="
  MTG_TREASURE_PAY_SOURCE=1 ./build/Release/mtg --batch "$MAN" --game-log-dir "$DIR" \
    2>&1 | grep -E "avg|WALL|heartbeat|SLOW-GAME" | tail -20
done
