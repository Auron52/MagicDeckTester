#!/usr/bin/env bash
# Unattended continuation (2026-08-19, user out ~2 h). Two user requests, run back to back
# so the box stays saturated and nothing waits on a human:
#
#   phase 1  NON-CLAIRVOYANT runs (user: "a smaller number of non-clairvoyant runs to see if
#            that changes anything"). Seed 980000 matches the overnight campaign exactly, so
#            the overnight's first 20,000 games ARE the clairvoyant baseline -- no re-run.
#            Two policies, because they answer different questions:
#              honest  MTG_HONEST_PLAY=1     1-sample draw-decoupled proxy, ~1x clairvoyant cost
#              k4d1    MTG_NC_SEARCH=1       true reshuffle-averaged NC, K=4 depth=1, ~5x
#            NOT the NC defaults (K=8 depth=2): ReshuffleAvgChoosePlan is called WITHOUT the
#            SearchBudget, so d=2 is unbounded -- measured 200-350 s PER GAME (~250x).
#   phase 2  analysis -> logs/nc/reports/REPORT.md
#   phase 3  the Oracle's Restoration / Impolite Entrance count screen (one pooled batch,
#            one shared apparatus, via deck_compare).
#
# Each phase is ONE pooled `mtg --batch` (or one deck_compare, which pools internally).
set -u -o pipefail
ROOT=/workspaces/MagicDeckTester
cd "$ROOT"
O=logs/nc
mkdir -p $O/reports $O/traces_k4d1 $O/batch
LOG=$O/run.log
say() { echo "[$(date -u +%H:%M:%S)] $*" | tee -a "$LOG"; }
GAMES=${GAMES:-20000}
SEED=980000

say "=== start; HEAD=$(git rev-parse --short HEAD)  games/arm=$GAMES  seed=$SEED"

python3 - "$GAMES" "$SEED" <<'PY' > $O/batch/manifest.json
import json, sys, os
games, seed = int(sys.argv[1]), int(sys.argv[2])
R = os.path.abspath("."); D = "logs/deckcmp/Mirrorwing Dragon"; V = "logs/overnight"
jobs = [{"name": a,
         "deck": f"{R}/{D}/{a}/Mirrorwing Dragon.txt",
         "deck_numbering": f"{R}/{D}/{a}/numbering.json",
         "games": games, "seed": seed, "max_turns": 8,
         "profile": f"{V}/app_{a}/Mirrorwing Dragon.profile.json",
         "value_profile": "decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.value.json",
         "value_model": False, "ladder_value_leaf": True}
        for a in ("base", "trick", "libonly")]
json.dump({"jobs": jobs}, sys.stdout, indent=1)
PY

PROV="$(readlink -f 'decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.cod')"

# ------------------------------------------------------------------ phase 1a: honest
say "phase 1a: MTG_HONEST_PLAY=1 (draw-decoupled 1-sample proxy)"
MTG_DUMP_WINS=1 MTG_PROVIDER_DECK="$PROV" MTG_HONEST_PLAY=1 \
  ./build/Release/mtg --batch $O/batch/manifest.json --threads 32 \
  > $O/batch/honest.out 2> $O/batch/honest.err
say "  done: $(grep -ac '^\[win\]' $O/batch/honest.err) win lines"

# ------------------------------------------------------------------ phase 1b: true NC
say "phase 1b: MTG_NC_SEARCH=1 K=4 DEPTH=1 (reshuffle-averaged non-clairvoyant) + traces"
MTG_DUMP_WINS=1 MTG_PROVIDER_DECK="$PROV" \
MTG_NC_SEARCH=1 MTG_NC_K=4 MTG_NC_DEPTH=1 \
  ./build/Release/mtg --batch $O/batch/manifest.json --threads 32 \
  --game-trace-dir $O/traces_k4d1 \
  > $O/batch/k4d1.out 2> $O/batch/k4d1.err
say "  done: $(grep -ac '^\[win\]' $O/batch/k4d1.err) win lines; traces $(ls $O/traces_k4d1 | wc -l)"

# ------------------------------------------------------------------ phase 2: analysis
say "phase 2: analysis"
python3 scripts/nc_analyse.py "$GAMES" > $O/reports/REPORT.md 2>> "$LOG" \
  && say "  report -> $O/reports/REPORT.md" || say "  analysis FAILED (see $LOG)"

# ------------------------------------------------------------------ phase 3: the screen
say "phase 3: Oracle's Restoration / Entrance-count screen (deck_compare, one pooled batch)"
# NO --with-floor here, deliberately. `--dry-run` showed this base deck has no keep table, so
# every arm falls back to the HEURISTIC mulligan. That is symmetric AND deterministic -- the
# apparatus-bias floor exists because stochastic R=10 tables disagree, and with no table there
# is no R sampling to disagree about. Bracketing it would have generated three R=10 tables
# (base, orest4, orest2_draught2) at hours each to bracket a table the screen never uses.
# What table-less DOES cost is symmetric play quality (~0.063t on every arm) and lookahead
# bottoming (slower per game). Disclose that; it is a SCREEN, and adoption re-measures anyway.
python3 scripts/deck_compare.py logs/deckcmp/entlib/spec.json \
  > $O/reports/screen.md 2> $O/reports/screen.err \
  && say "  screen -> $O/reports/screen.md" || say "  screen FAILED (see $O/reports/screen.err)"

say "=== complete"
