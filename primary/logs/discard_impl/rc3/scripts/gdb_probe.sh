#!/bin/bash
# Run the snow stuck game (gi=221 seed 1540222, ship cell) under gdb so its stack can be sampled.
# usage: gdb_probe.sh <outdir> <arm: old|new> [extra VAR=VAL ...]
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
OUT=$1; ARM=$2; shift 2
mkdir -p "$OUT"
BIN=/workspaces/MagicDeckTester/logs/discard_impl/ab3/mtg_ab3_d6a760e5
OLDENV=""
if [ "$ARM" = old ]; then
  OLDENV="MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0 MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0 MTG_SOLVE_PENDING_AURA_EXACT=0"
fi
CMDS=$OUT/cmds.gdb
{
  echo "set pagination off"
  echo "set print frame-arguments none"
  echo "set print entry-values no"
  echo "handle SIGINT stop print nopass"
  echo "handle SIGPIPE nostop noprint pass"
  echo "run"
  for i in $(seq 1 400); do
    echo "echo ==== SAMPLE $i\\n"
    echo "thread apply all bt 150"
    echo "thread apply all -s p (long long)'gamework::t_used'"
    echo "thread apply all -s p (long long)'decisionwork::t_used'"
    echo "thread apply 1 -s p (long long)g_fs_trunc_events"
    echo "continue"
  done
} > "$CMDS"
# shellcheck disable=SC2086
env $OLDENV MTG_DECISION_PROGRESS=1 MTG_TURN_CENSUS=$OUT/census.tsv "$@" nice -n 19 gdb -q -batch -x "$CMDS" --args "$BIN" \
  /workspaces/MagicDeckTester/decks/Snow/Snow.cod --profile /workspaces/MagicDeckTester/decks/Snow/Snow.profile.json \
  --seed 1540222 --game-index 221 --games 1 --budget-ms 20 --threads 1 --log-dir "$OUT" \
  > "$OUT/gdb_stdout.txt" 2> "$OUT/stderr.txt"
echo "gdb exit=$?" >> "$OUT/gdb_stdout.txt"
