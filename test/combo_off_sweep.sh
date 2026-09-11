#!/usr/bin/env bash
# COMBO OFF SWEEP -- the wide version of test/combo_off_check.sh.
#
# combo_off_check.sh pins TEN hand-authored boards.  This sweeps HUNDREDS, over three populations
# (a synthetic matrix across the rule table's ingredients, the user's saved reference games, and
# autonomous engine play at the deck's shipped settings), and sorts every one into offered+wins /
# offered+NO-WIN (executor failure) / missed offer / rule-too-loose / correctly absent.
#
# USER, 2026-09-10: "We should do a bunch of testing of Combo Off states and fix any case that
# fails to go off", and "the intention is to use the same logic to skip work for the search" --
# which is why the autonomous population is reported separately with FALSE FIRE / MISSED FIRE
# rates: a false fire inside the search would corrupt evaluation and ground truth.
#
# This is a REPORT, not a gate (same contract as test/combo_off_frames.py): it exits nonzero only
# on a HARNESS error, never on a finding.  Gating on the class counts is phase 2's business, once
# the clusters in docs/design/combo-off-sweep-catalogue.md are fixed.
#
# Usage:  bash test/combo_off_sweep.sh                  # full sweep, all three populations
#         bash test/combo_off_sweep.sh --quick          # gate mode: synthetic core + 12 games
#         bash test/combo_off_sweep.sh --games 200      # deeper autonomous mining
#         bash test/combo_off_sweep.sh --reuse-games    # RE-RUN AFTER A FIX: mine the game logs
#                                                       # already on disk, so the before/after
#                                                       # sweeps compare the SAME states
#         MTG_BIN=path bash test/combo_off_sweep.sh
#
# MEASURED (24-core box, shared): --quick 216 states in 3m18s; the full sweep 1037 states in about
# 4 min once the game logs exist. Nearly all of it is the autonomous game generation, which is ONE
# pooled `--games N --threads T` invocation (never a per-seed loop) and is the sweep's only
# barrier -- a genuine data dependency, since the mined states do not exist until the games have
# been played. The probe phase is one pooled thread-pool queue over every state of every
# population, so it drains to a single tail.
#
# A STRAGGLER GAME CAN DOMINATE. This deck has degenerate games (one of 60 ran >10 min while the
# other 59 finished in ~5 min total). `--reuse-games` exists partly for that: the logs already
# written are perfectly good states to mine.
#
# Results land in logs/combo_off_sweep/results.json -- machine-readable and keyed by STABLE state
# id, so a re-run after a fix diffs cleanly against the previous one.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
BIN="${MTG_BIN:-$root/build/Release/mtg}"
[ -f "$BIN" ] || BIN="$root/build/Release/mtg.exe"
if [ ! -f "$BIN" ]; then
  echo "ERROR: $BIN not found -- build Release first (./build.sh)." >&2
  exit 2
fi
cd "$root" || exit 2
exec python3 "$here/combo_off_sweep.py" "$@"
