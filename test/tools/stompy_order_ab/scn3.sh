#!/usr/bin/env bash
# A scenario on the current fix binary (mtg.fix2) under four arms: ofix (order + fix), onotop (order + fix,
# no TOP_RESOLVE), fix (order off + fix), off (both off). usage: scn3.sh <scenario.json> <out_dir> [ENV=VAL ...]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
F=$1; OUT=$2; shift 2
mkdir -p "$OUT"
cd "$WT" || exit 1
for arm in ofix onotop fix off; do
  case $arm in
    ofix)   E="MTG_STOMPY_ORDER=1 MTG_HOLD_RELEASE_YIELD=1" ;;
    onotop) E="MTG_STOMPY_ORDER=1 MTG_HOLD_RELEASE_YIELD=1 MTG_STOMPY_TOP_RESOLVE=0" ;;
    fix)    E="MTG_STOMPY_ORDER=0 MTG_HOLD_RELEASE_YIELD=1" ;;
    off)    E="MTG_STOMPY_ORDER=0 MTG_HOLD_RELEASE_YIELD=0" ;;
  esac
  env "$@" $E MTG_BATCH_HEARTBEAT="$OUT/hb.txt" MTG_USER_ORDER_TRACE=1 MTG_FD_TRACE=1 nice -n 10 "$SC/bin/mtg.fix2" \
      --scenario "$F" --cards-json "$WT/src/cards/data/cards.json" > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  echo "== $arm: $(grep '^scenario: win_turn' "$OUT/$arm.out")"
  grep -E "^\[fd\] T[0-9] LINE" "$OUT/$arm.err" | head -2 | cut -c1-400
done
