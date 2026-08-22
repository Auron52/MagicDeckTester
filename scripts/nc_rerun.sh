#!/usr/bin/env bash
# NC re-run after the 2026-08-19 OOM kill.
#
# What happened: MTG_NC_SEARCH=1 K=4 DEPTH=1 at --threads 32 reached 23 GB RSS -- the whole
# box -- and the kernel killed mtg mid-batch, leaving 2 of 3 arms with zero games. The NC
# path is not budget-bounded (ReshuffleAvgChoosePlan takes no SearchBudget), so each of the
# 32 concurrent games can grow its search structures without a cap; 32 x that is the box.
#
# Three changes, all aimed at peak RSS rather than at speed:
#   --threads 10   fewer CONCURRENT unbounded games (the multiplier that broke it)
#   MTG_NC_K=2     half the reshuffles per decision
#   no traces      the previous run wrote 15,402 trace files for a run that never finished
# and a smaller sample, which is what the user asked for ("a smaller number of
# non-clairvoyant runs"). 6,000 games/arm still resolves the ~0.03t headline at se ~0.012.
#
# Waits for the in-flight screen to finish first: running both would re-create the very
# memory pressure this is fixing, and would strand cores on two competing pools.
set -u -o pipefail
cd /workspaces/MagicDeckTester
O=logs/nc
LOG=$O/rerun.log
say() { echo "[$(date -u +%H:%M:%S)] $*" | tee -a "$LOG"; }

say "waiting for the in-flight screen (pid ${WAIT_PID:-none}) to finish"
while pgrep -f "deck_compare.py logs/deckcmp/entlib/spec.json" > /dev/null; do sleep 30; done
while pgrep -f "entlib/screen.manifest.json" > /dev/null; do sleep 30; done
say "screen finished; starting NC re-run"

GAMES=${GAMES:-6000}
python3 - "$GAMES" <<'PY' > $O/batch/manifest_small.json
import json, sys, os
games = int(sys.argv[1])
R = os.path.abspath("."); D = "logs/deckcmp/Mirrorwing Dragon"; V = "logs/overnight"
jobs = [{"name": a,
         "deck": f"{R}/{D}/{a}/Mirrorwing Dragon.txt",
         "deck_numbering": f"{R}/{D}/{a}/numbering.json",
         "games": games, "seed": 980000, "max_turns": 8,
         "profile": f"{V}/app_{a}/Mirrorwing Dragon.profile.json",
         "value_profile": "decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.value.json",
         "value_model": False, "ladder_value_leaf": True}
        for a in ("base", "trick", "libonly")]
json.dump({"jobs": jobs}, sys.stdout, indent=1)
PY

say "NC: K=2 depth=1, threads 10, ${GAMES} games/arm"
MTG_DUMP_WINS=1 MTG_PROVIDER_DECK="$(readlink -f 'decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.cod')" \
MTG_NC_SEARCH=1 MTG_NC_K=2 MTG_NC_DEPTH=1 \
  ./build/Release/mtg --batch $O/batch/manifest_small.json --threads 10 \
  > $O/batch/nc2.out 2> $O/batch/nc2.err
rc=$?
say "  exit=$rc; win lines: $(grep -ac '^\[win\]' $O/batch/nc2.err)"
say "  per-arm: $(grep -ao '^\[win\] job=[a-z]*' $O/batch/nc2.err | sort | uniq -c | tr '\n' ' ')"

# nc_analyse.py REFUSES a truncated run (the guard added after the OOM), so a second kill
# fails loudly instead of printing a fake effect.
python3 scripts/nc_analyse.py "$GAMES" > $O/reports/REPORT.md 2>> "$LOG" \
  && say "report -> $O/reports/REPORT.md" || say "analysis REFUSED or failed (see $LOG)"
say "=== NC re-run complete"
