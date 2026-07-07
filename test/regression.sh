#!/usr/bin/env bash
# Regression tester for the MTG simulator. Three modes, each with its own time
# budget, cadence, and (deliberately disjoint) seeds so coverage compounds when
# you run more than one:
#
#   --smoke       fast gate, target < 15 min, run frequently / before every push
#   (default)     pre-commit regression, target < 45 min, run before committing
#   --overnight   deep multi-seed sweep, target < 8 h, run while you sleep
#
# Each case is (deck x depth x seed x games x budget), defined in
# regression_cases.sh. The whole matrix is emitted as ONE manifest and run via
# `mtg.exe --batch`, which pools every game of every case into a single work queue
# so the suite pays one load-imbalance tail instead of one per case (results are
# byte-identical to per-case runs -- see docs/design/batch-runner.md). We then:
#   * write the manifest and full batch output to test/logs/<mode>/ (+ batch.err),
#   * record each case's fingerprint "<games_won>/<avg_win_turn>" to
#     test/results/<mode>.env,
#   * compare that fingerprint to the committed ground truth in regression_gt.txt
#     (keyed <deck>_<mode>_d<depth>_s<seed>).
# The won-count catches win<->loss flips that barely move the avg win turn
# (important for decks like Treasure Hunt that do not always win).
#
# Usage (run from repo root, after building Release):
#   bash test/regression.sh            # regression mode (default)
#   bash test/regression.sh --smoke    # fast smoke gate
#   bash test/regression.sh --overnight
#
# Update ground truth from a run you have inspected and ACCEPT (no re-run):
#   bash test/regression.sh --smoke --accept     # promote last smoke results
# Accept reuses test/results/<mode>.env from the most recent run of that mode and
# merges it into regression_gt.txt, leaving the other modes untouched.
#
# Thread count defaults to hardware_concurrency (--threads 0). Results are
# thread-invariant, so the ground truth is valid at any thread count.
#   THREADS=2 bash test/regression.sh
#
# Exit code: 0 = all pass (NEW keys do not fail), 1 = any mismatch.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Multi-config build layout: build/Release/mtg.exe on Windows (MSVC),
# build/Release/mtg in the Linux dev container (Ninja Multi-Config).
BIN=./build/Release/mtg.exe
[ -f "$BIN" ] || BIN=./build/Release/mtg
GT=test/regression_gt.txt
THREADS=${THREADS:-0}

MODE=regression
ACCEPT=0
ACCEPT_ACK=""     # --accept-with-regressions=<ack>: acknowledge each searched win->loss so
                  # --accept may proceed; the ack string is recorded in the GT provenance header.
DECK_ONLY=""      # --deck=<name>: restrict this run to one deck's cases (see regression_cases.sh)
for arg in "$@"; do
  case "$arg" in
    --smoke)     MODE=smoke ;;
    --overnight) MODE=overnight ;;
    --fast)      MODE=smoke ;;      # back-compat alias
    --accept)    ACCEPT=1 ;;
    --accept-with-regressions=*) ACCEPT=1; ACCEPT_ACK="${arg#*=}" ;;
    --deck=*)    DECK_ONLY="${arg#*=}" ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

LOGDIR=test/logs/$MODE
OUT=test/regression_result_${MODE}.txt
RESULTS=test/results/${MODE}.env
mkdir -p "$LOGDIR" test/results

# Flag that selects this mode on the command line (regression is the default).
case "$MODE" in
  smoke)     MODEFLAG="--smoke" ;;
  overnight) MODEFLAG="--overnight" ;;
  *)         MODEFLAG="" ;;
esac

# shellcheck source=regression_cases.sh
source "$HERE/regression_cases.sh"

case "$MODE" in
  smoke)      CASES=( "${SMOKE_CASES[@]}" ) ;;
  regression) CASES=( "${REGRESSION_CASES[@]}" ) ;;
  overnight)  CASES=( "${OVERNIGHT_CASES[@]}" ) ;;
esac

# --deck=<name>: keep only that deck's cases (the run path -- manifest, compare, per-game diff,
# and .wins promotion all iterate CASES). The aggregate-GT rebuild on --accept still iterates the
# FULL mode arrays (sourcing existing GT first), so a per-deck accept updates only this deck's keys
# and leaves every other deck's ground truth intact.
if [ -n "$DECK_ONLY" ]; then
  _filtered=()
  for spec in "${CASES[@]}"; do
    # shellcheck disable=SC2086
    set -- $spec
    [ "$1" = "$DECK_ONLY" ] && _filtered+=("$spec")
  done
  [ ${#_filtered[@]} -eq 0 ] && { echo "ERROR: --deck=$DECK_ONLY matched no $MODE cases" >&2; exit 2; }
  CASES=( "${_filtered[@]}" )
fi

# ---- accept: promote the last run's results into ground truth -------------
# Rebuilds regression_gt.txt in canonical matrix order, pulling each value from
# (existing ground truth) overlaid with (this mode's just-accepted results), so
# only the accepted mode changes. No binary is run.
if [ "$ACCEPT" = 1 ]; then
  if [ ! -f "$RESULTS" ]; then
    echo "ERROR: $RESULTS not found. Run 'bash test/regression.sh $MODEFLAG' first, inspect it, then --accept." >&2
    exit 1
  fi
  # ---- pre-accept gate: the per-game audit must show no unexplained searched-depth
  # win->loss. gt_logs still holds the PRE-accept baseline and $LOGDIR/wins holds the
  # last run's per-game outcomes, so this is exactly the old-vs-new per-game diff. A
  # searched-depth win->loss (audit exit != 0) hard-blocks promotion unless every one
  # is acknowledged via --accept-with-regressions="gi<N>:<reason>; ...". Turn-later is
  # surfaced but does not block (it must still be classified -- see the audit output).
  if [ -f "$HERE/audit_changed_games.py" ] && command -v python3 >/dev/null 2>&1; then
    echo "--- pre-accept per-game audit ($MODE) ---"
    audit_out=$(python3 "$HERE/audit_changed_games.py" "$MODE" 2>&1); audit_rc=$?
    printf '%s\n' "$audit_out"
    if [ "$audit_rc" -ne 0 ] && [ -z "$ACCEPT_ACK" ]; then
      echo "" >&2
      echo "REFUSING TO ACCEPT: audit reports searched-depth win->loss (above)." >&2
      echo "Root-cause each, then re-run with:" >&2
      echo "  bash test/regression.sh $MODEFLAG --accept-with-regressions=\"gi<N>:<reason>; ...\"" >&2
      exit 1
    fi
    [ -n "$ACCEPT_ACK" ] && echo "Proceeding with acknowledged regressions: $ACCEPT_ACK"
  fi
  # shellcheck disable=SC1090
  [ -f "$GT" ] && source "$GT" 2>/dev/null || true   # existing values for all modes
  # shellcheck disable=SC1090
  source "$RESULTS"                                   # override the accepted mode
  emit_mode() {
    local mode=$1; local -n arr=$2
    echo ""
    echo "# --- $mode ---"
    local spec deck depth seed key val
    for spec in "${arr[@]}"; do
      # shellcheck disable=SC2086
      set -- $spec; deck=$1; depth=$2; seed=$3
      key="${deck}_${mode}_d${depth}_s${seed}"
      val="${!key-}"
      [ -n "$val" ] && [ "$val" != "TODO" ] && echo "$key=$val"
    done
  }
  {
    echo "# Regression ground truth -- commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)  date $(date +%Y-%m-%d)"
    echo "# Promoted from accepted runs by 'regression.sh --accept' -- do not hand-edit."
    [ -n "$ACCEPT_ACK" ] && echo "# accepted-with-regressions ($MODE, $(date +%Y-%m-%d)): $ACCEPT_ACK"
    echo "# Key: <deck>_<mode>_d<depth>_s<seed> = <games_won>/<avg_win_turn>[/<play_digest>]"
    echo "# Modes: smoke (<15m), regression (<45m), overnight (<8h); seeds disjoint."
    emit_mode smoke      SMOKE_CASES
    emit_mode regression REGRESSION_CASES
    emit_mode overnight  OVERNIGHT_CASES
  } > "$GT.tmp"
  mv "$GT.tmp" "$GT"

  # Promote this mode's per-game logs (from the last run) into the committed
  # ground-truth logs, so future runs diff against the accepted per-game outcomes.
  mkdir -p test/gt_logs
  promoted=0
  for spec in "${CASES[@]}"; do
    # shellcheck disable=SC2086
    set -- $spec; deck=$1; depth=$2; seed=$3
    key="${deck}_${MODE}_d${depth}_s${seed}"
    if [ -f "$LOGDIR/wins/${key}.wins" ]; then
      cp "$LOGDIR/wins/${key}.wins" "test/gt_logs/${key}.wins"; promoted=$((promoted+1))
    fi
  done
  # Save the binary that produced these accepted results as the per-mode BASELINE, so the next
  # run's audit can diff current-vs-baseline per game (explain_game.py) with no rebuild. The run
  # snapshotted its exact binary to $LOGDIR/mtg.run (dirty-state-safe); it now IS the baseline.
  # Under logs/snapshots/ (gitignored): a fresh clone lacks it and explain_game falls back to the
  # NEW-line-only view until the first local --accept re-creates it.
  if [ -f "$LOGDIR/mtg.run" ]; then
    mkdir -p logs/snapshots
    cp -f "$LOGDIR/mtg.run" "logs/snapshots/${MODE}-baseline"
    [ -f "$LOGDIR/mtg.run.meta" ] && cp -f "$LOGDIR/mtg.run.meta" "logs/snapshots/${MODE}-baseline.meta"
    [ -f "$LOGDIR/mtg.run.diff" ] && cp -f "$LOGDIR/mtg.run.diff" "logs/snapshots/${MODE}-baseline.diff"
    echo "Saved baseline binary -> logs/snapshots/${MODE}-baseline (for next run's per-game diff)."
  fi

  echo "Accepted $MODE results into $GT (and $promoted per-game log(s) into test/gt_logs/)."
  exit 0
fi

# ---- run a mode and compare ----------------------------------------------
if [ ! -f "$BIN" ]; then
  echo "ERROR: $BIN not found. Run cmake --build build --config Release first." >&2
  exit 1
fi

# Snapshot the binary into the run dir and execute the COPY. A long run can then
# overlap with rebuilds of build/Release/mtg (iterating on other work) without
# hitting ETXTBSY ("Text file busy", which the linker raises when it opens a running
# executable for write) or silently swapping the binary mid-run. The snapshot is
# what every batch invocation below uses; the source binary is free to be rebuilt.
BIN_SNAPSHOT="$LOGDIR/mtg.run"
cp -f "$BIN" "$BIN_SNAPSHOT"
# Provenance stamp: record the exact version this run (and any --accept'd GT) came from.
# When the tree is dirty, save the working diff so a re-baseline from uncommitted changes
# stays reconstructable as git_hash + mtg.run.diff (see test/snapshot_bin.sh).
{
  echo "git_hash=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  if git diff --quiet HEAD 2>/dev/null; then echo "git_state=clean"; else
    echo "git_state=dirty"; git diff HEAD > "$BIN_SNAPSHOT.diff" 2>/dev/null || true
    echo "diff=mtg.run.diff"
  fi
  echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$BIN_SNAPSHOT.meta" 2>/dev/null || true
BIN="$BIN_SNAPSHOT"

[ -f "$GT" ] && source "$GT" 2>/dev/null || true

PASS=0; FAIL=0; NEW=0

log() { echo "$1"; echo "$1" >> "$OUT"; }

: > "$OUT"
: > "$RESULTS"
log "=== REGRESSION ($MODE) $(date) ==="
log "(threads=$THREADS; binary=$BIN; logs in $LOGDIR)"

# Scenario sanity gate: hand-built board fixtures (test/scenarios/*.json) that assert a specific
# interaction still plays correctly. Cheap (seconds) and deck-agnostic, so run them up front on the
# freshly-built binary. A FAIL here is a hard regression -- abort before the (long) batch run.
SCEN="$(dirname "$0")/scenarios.sh"
if [ -f "$SCEN" ]; then
  log "--- scenario sanity ---"
  if MTG_BIN="$BIN" bash "$SCEN" | tee -a "$OUT"; then :; else
    log "ABORT: scenario sanity failed (a hand-built fixture regressed) -- fix before running the batch."
    exit 1
  fi
fi

# Viewer sanity gate: guard the play GUI (tools/play) by replaying the saved reference games,
# in two deck-agnostic layers. A hard FAIL aborts before the (long) batch, like the scenario gate.
#   * line-build (frontend)   -- viewer_linebuild_check.js drives the REAL browser queue logic
#     (tools/play/linebuild.js): can the GUI still rebuild every line the user actually played?
#     Sub-second, no binary -> run in ALL modes. Catches viewer-layer regressions the protocol
#     check is blind to (e.g. a staged/exiled cast queueCard silently drops).
#   * protocol (engine<->GUI) -- viewer_protocol_check.py replays each reference's chosen plan
#     indices through the binary and asserts the decision-JSON contract holds (well-formed, valid
#     index, clean terminal). It re-invokes the binary per step, so the full reference set is
#     MULTI-MINUTE (~35min alone) -> SHARDED: regression runs a fast one-ref-per-deck SAMPLE
#     (--sample: contract sanity across every archetype), and OVERNIGHT runs the FULL sweep. The
#     contract does not vary by seed set, so the sample loses no coverage the nightly full run
#     doesn't restore, and regression stays inside its <45min budget. smoke stays binary-free
#     (line-build only). Run WITHOUT --strict: behaviour drift (won/win_turn changed) is
#     informational (re-save the reference when satisfied); only a CONTRACT break (exit 1) aborts.
if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_linebuild_check.js" ]; then
  log "--- viewer line-build check (frontend) ---"
  if node "$HERE/viewer_linebuild_check.js" | tee -a "$OUT"; then :; else
    log "ABORT: viewer line-build check failed (the GUI cannot rebuild a played reference line)."
    exit 1
  fi
fi
if { [ "$MODE" = regression ] || [ "$MODE" = overnight ]; } && command -v python3 >/dev/null 2>&1 \
   && [ -f "$HERE/viewer_protocol_check.py" ]; then
  # regression -> --sample (one ref/deck, fast); overnight -> full sweep (all refs).
  PROTO_ARGS=""; [ "$MODE" = regression ] && PROTO_ARGS="--sample"
  log "--- viewer protocol check (engine<->GUI contract${PROTO_ARGS:+, $PROTO_ARGS}) ---"
  if MTG_BIN="$BIN" python3 "$HERE/viewer_protocol_check.py" $PROTO_ARGS | tee -a "$OUT"; then :; else
    log "ABORT: viewer protocol check reported a CONTRACT failure (malformed/invalid decision)."
    exit 1
  fi
fi

# Emit the whole case matrix as one batch manifest. `mtg.exe --batch` pools every
# game of every case into a single atomic work queue, so the suite pays ONE
# load-imbalance tail instead of one per case (the old per-case sweep stranded a
# core on each config's slowest game). Results are validated byte-identical to the
# per-case runs, so the ground truth is unchanged. See docs/design/batch-runner.md.
MANIFEST="$LOGDIR/manifest.json"
{
  echo '{ "jobs": ['
  first=1
  for spec in "${CASES[@]}"; do
    # shellcheck disable=SC2086
    set -- $spec; deck=$1; depth=$2; seed=$3; games=$4; budget=$5
    file=${DECK_FILE[$deck]}; prof=${DECK_PROF[$deck]}
    name="${deck}_${MODE}_d${depth}_s${seed}"
    # depth>0 searches with its budget; depth 0 is the clean greedy baseline (budget 0).
    # Lookahead bottoming is no longer a flag -- the engine derives it from depth (ON iff
    # depth>0), so d0 automatically runs without bottoming (its greedy rollout cannot
    # discriminate on a deep London mulligan and would bottom the payoff, a d0-only misplay).
    if [ "$depth" -gt 0 ]; then bud=$budget; else bud=0; fi
    # LPT scheduling weight (see BatchRunner Job::sched_weight): Hinata's deep search measured ~40x
    # the other decks per game (heavy multi-minute tail), so without a boost its d3/d5 games sort
    # behind every deck's d5 and become the long tail that dominates the makespan. Give Hinata
    # depth>0 a high weight so
    # those games start FIRST and the cheap games backfill while they grind. d5 outranks d3. Other
    # jobs keep weight 0 (the depth/budget proxy). Ordering is lossless -- results are unchanged.
    weight=0
    if [ "$deck" = "hinata" ] && [ "$depth" -gt 0 ]; then weight=$((depth * 1000 + bud)); fi
    [ $first -eq 1 ] && first=0 || printf ',\n'
    printf '  { "name": "%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "depth": %s, "budget_ms": %s, "weight": %s }' \
      "$name" "$file" "$prof" "$games" "$seed" "$depth" "$bud" "$weight"
  done
  printf '\n] }\n'
} > "$MANIFEST"

TOTAL_START=$(date +%s)
# `tee` to batch.log so per-job results STREAM to the terminal live as each case
# finishes (the binary flushes one line per completed job), instead of being buffered
# inside $() until the whole batch ends. We then read the captured file for parsing.
"$BIN" --batch "$MANIFEST" --threads "$THREADS" --game-log-dir "$LOGDIR/wins" \
    2>"$LOGDIR/batch.err" | tee "$LOGDIR/batch.log"
TOTAL=$(( $(date +%s) - TOTAL_START ))
BATCH_OUT=$(cat "$LOGDIR/batch.log")

# Per-game ground-truth logs live in test/gt_logs/<key>.wins (committed). The batch
# run just wrote this run's per-game win turns to $LOGDIR/wins/<key>.wins. We diff
# them so the report names EXACTLY which games changed -- the cheap built-in version
# of the mandatory pre-accept per-game analysis (no rebuilding the old binary).
GTLOGS=test/gt_logs

# Parse one "<name>: played=P won=W (pct%) avg=A" line per job into the existing
# won/avg fingerprint, in matrix order, and compare to ground truth.
CUR_DECK=""
for spec in "${CASES[@]}"; do
  # shellcheck disable=SC2086
  set -- $spec; deck=$1; depth=$2; seed=$3
  if [ "$deck" != "$CUR_DECK" ]; then CUR_DECK="$deck"; log ""; log "-- $CUR_DECK --"; fi
  key="${deck}_${MODE}_d${depth}_s${seed}"
  line=$(printf '%s\n' "$BATCH_OUT" | grep "^${key}: ")
  won=$(printf '%s\n' "$line" | sed -nE 's/.*won=([0-9]+).*/\1/p')
  awt=$(printf '%s\n' "$line" | sed -nE 's/.*avg=([0-9.]+).*/\1/p')
  dg=$(printf '%s\n' "$line" | sed -nE 's/.*digest=([0-9a-f]+).*/\1/p')
  expected="${!key-}"
  if [ -z "$won" ] || [ -z "$awt" ]; then
    status="FAIL"; got="(no output)"; FAIL=$((FAIL+1))
  else
    # Fingerprint = won/avg/play-digest. The digest makes a play change that keeps the same
    # win counts/turns still FAIL (the coarse won/avg cannot see it). Backward-compat: a legacy
    # 2-field GT (no digest baselined yet) matches on won/avg alone -- PASS, and --accept records
    # the digest so subsequent runs gate on it too.
    got="${won}/${awt}${dg:+/$dg}"
    echo "$key=$got" >> "$RESULTS"           # record for a later --accept
    if [ -z "$expected" ]; then
      status="NEW "; expected="<none>"; NEW=$((NEW+1))
    elif [ "$expected" = "$got" ]; then
      status="PASS"; PASS=$((PASS+1))
    elif [ "$expected" = "${won}/${awt}" ]; then
      status="PASS"; PASS=$((PASS+1))        # legacy GT without a digest: won/avg match
    else
      status="FAIL"; FAIL=$((FAIL+1))
    fi
  fi
  log "$(printf '  %s  %-26s exp=%-12s got=%-12s' "$status" "$key" "$expected" "$got")"

  # Per-game diff against committed ground-truth logs. Lists every game whose win
  # turn moved (old -> new), so changed games can be inspected before --accept.
  new_wins="$LOGDIR/wins/${key}.wins"; gt_wins="$GTLOGS/${key}.wins"
  if [ -f "$new_wins" ] && [ -f "$gt_wins" ]; then
    diffs=$(awk 'FNR==NR{o[$1]=$2;next} ($1 in o)&&o[$1]!=$2{print "      gi="$1": "o[$1]" -> "$2}' \
                "$gt_wins" "$new_wins")
    if [ -n "$diffs" ]; then
      nch=$(printf '%s\n' "$diffs" | grep -c .)
      log "      >> $nch game(s) changed vs ground-truth log (inspect before --accept):"
      printf '%s\n' "$diffs" | head -20 | while IFS= read -r dl; do log "$dl"; done
      [ "$nch" -gt 20 ] && log "      ... ($((nch-20)) more)"
    fi
  elif [ -f "$new_wins" ] && [ ! -f "$gt_wins" ]; then
    log "      >> no ground-truth log yet ($GTLOGS/${key}.wins) -- will be created on --accept"
  fi
done

# ---- per-game audit (split by depth) -- makes the pre-accept analysis unmissable ----------
# The fingerprint compare above governs PASS/FAIL; this appends the per-game flip breakdown the
# aggregate hides (win->loss / turn-later, split searched vs d0) plus the list of every searched
# flip, so an ordinary run already surfaces exactly what a later --accept must have explained. The
# same audit hard-gates --accept (see the ACCEPT block). See docs/design/auto-audit-integration.md.
if [ -f "$HERE/audit_changed_games.py" ] && command -v python3 >/dev/null 2>&1; then
  log ""
  log "--- per-game audit (vs committed gt_logs) ---"
  audit_out=$(python3 "$HERE/audit_changed_games.py" "$MODE" 2>&1); audit_rc=$?
  printf '%s\n' "$audit_out" | while IFS= read -r al; do log "$al"; done
  if [ "$audit_rc" -ne 0 ]; then
    log "      >> searched-depth win->loss present -- --accept will be BLOCKED until each is"
    log "         root-caused (then acknowledged via --accept-with-regressions=...)."
  fi
  # Offer the churn auto-classifier when there are searched turn-later games to explain.
  if printf '%s\n' "$audit_out" | grep -q "SEARCHED-depth turn-later"; then
    log "      >> classify searched turn-later automatically: bash test/classify_turn_later.sh $MODE"
  fi
fi

log ""
log "Result: $PASS passed, $FAIL failed, $NEW new   (batch makespan ${TOTAL}s = $((TOTAL/60))m$((TOTAL%60))s)"
if [ "$FAIL" -eq 0 ]; then
  [ "$NEW" -gt 0 ] && log "ALL PASS ($NEW new key(s); inspect, then 'bash test/regression.sh $MODEFLAG --accept' to record)" \
                   || log "ALL PASS"
  exit 0
else
  log "REGRESSION DETECTED"
  exit 1
fi
