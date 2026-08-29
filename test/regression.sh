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
#   * record each case's fingerprint "<avg>[/<play_digest>]" to test/results/<mode>.env
#     (avg = mean turn-to-win, an unwon game scored max_turns+1; win/loss is not reported),
#   * compare that fingerprint to the committed ground truth in regression_gt.txt
#     (keyed <deck>_<mode>_d<depth>_s<seed>).
# An unwon game folds into the avg at max_turns+1 (the loss-penalized metric), so a game going
# from a win to unwon shows up directly as a worse avg -- no separate win-count needed
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
# Shared harness primitives (binary resolution, manifest/batch/metric helpers) -- see
# test/lib/harness.sh. This suite is the reference implementation those helpers were extracted
# from; it deliberately keeps its own manifest emitter (the value_play depth rules and the Hinata
# LPT scheduling weight are suite policy, and the manifest bytes are load-bearing for the GT).
# shellcheck source=lib/harness.sh
. "$HERE/lib/harness.sh"
# Multi-config build layout: build/Release/mtg.exe on Windows (MSVC),
# build/Release/mtg in the Linux dev container (Ninja Multi-Config).
BIN=$(harness_bin) || exit 1
GT=test/regression_gt.txt
THREADS=${THREADS:-0}

MODE=regression
ACCEPT=0
ACCEPT_ACK=""     # --accept-with-regressions=<note>: same as --accept (promote, no re-run/re-audit);
                  # the note is recorded in the GT provenance header to document an intended slowdown.
DECK_ONLY=""      # --deck=<name>: restrict this run to one deck's cases (see regression_cases.sh)
for arg in "$@"; do
  case "$arg" in
    --smoke)     MODE=smoke ;;
    --regression) MODE=regression ;;   # the default; accepted explicitly so a scripted sweep can
                                       # pass a mode flag UNIFORMLY instead of special-casing ""
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

# --deck=<name>[,<name>...]: keep only those decks' cases (the run path -- manifest, compare,
# per-game diff, and .wins promotion all iterate CASES). The aggregate-GT rebuild on --accept still
# iterates the FULL mode arrays (sourcing existing GT first), so a per-deck accept updates only
# those decks' keys and leaves every other deck's ground truth intact.
#
# A COMMA LIST matters for A/B sweeps: an arm that touches three decks must run them as ONE pooled
# batch (repo policy -- one load-imbalance tail, not one per deck), and running the full suite
# instead would spend the majority of the makespan on decks the flag cannot reach.
if [ -n "$DECK_ONLY" ]; then
  _filtered=()
  for spec in "${CASES[@]}"; do
    # shellcheck disable=SC2086
    set -- $spec
    for _d in ${DECK_ONLY//,/ }; do
      [ "$1" = "$_d" ] && { _filtered+=("$spec"); break; }
    done
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
  # --accept means "I have inspected this run and these results are the new ground truth" -- so it
  # ONLY promotes; it does NOT re-run the games and does NOT re-run the per-game audit. Inspect the
  # audit in the RUN output first (a plain `regression.sh <mode>` prints the per-game audit + the
  # searched-depth SLOWER list at the end), decide there, then --accept to promote. The optional
  # --accept-with-regressions="<note>" records WHY any accepted slowdowns are intended into the GT
  # provenance header (below) -- it is documentation, not a gate.
  [ -n "$ACCEPT_ACK" ] && echo "Accepting with recorded note: $ACCEPT_ACK"
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
  # CARRY FORWARD prior acknowledgements. The GT file is fully REGENERATED here, and the ack line
  # used to be emitted only for THIS accept -- so accepting any OTHER mode silently erased it. That
  # is a provenance loss, not a cosmetic one: the note is the only record that a set of cells was
  # deliberately accepted while FAILING, and which are still carried on that basis. It went wrong
  # exactly once (a mirrorwing smoke accept dropped minotaur's overnight note, 2026-08-29), and the
  # cost of noticing is high -- the values it explains are untouched, so nothing looks wrong.
  # An ack for the mode being accepted NOW is dropped from the carried set: this run supersedes it.
  prior_acks=""
  if [ -f "$GT" ]; then
    prior_acks="$(grep -E '^# accepted-with-regressions \(' "$GT" 2>/dev/null \
                  | grep -vE "^# accepted-with-regressions \($MODE," || true)"
  fi
  {
    echo "# Regression ground truth -- commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)  date $(date +%Y-%m-%d)"
    echo "# Promoted from accepted runs by 'regression.sh --accept' -- do not hand-edit."
    [ -n "$prior_acks" ] && echo "$prior_acks"
    [ -n "$ACCEPT_ACK" ] && echo "# accepted-with-regressions ($MODE, $(date +%Y-%m-%d)): $ACCEPT_ACK"
    echo "# Key: <deck>_<mode>_d<depth>_s<seed> = <avg>[/<play_digest>]   (avg = mean turn-to-win, unwon = max_turns+1)"
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
    # Promote ONLY keys the accepted run actually measured (present in $RESULTS). The wins dir
    # accumulates stale .wins from earlier per-deck runs of OTHER decks; an accept without the
    # matching --deck filter used to promote those stale files over good committed GT (fired
    # twice on 2026-08-19: 9 regression + 8 overnight logs from older binaries, caught only by
    # the pre-commit git diff). The RESULTS env is the inspected artifact -- it defines exactly
    # what "this run" covers, for both full and per-deck accepts.
    if [ -f "$LOGDIR/wins/${key}.wins" ] && grep -q "^${key}=" "$RESULTS"; then
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
  # Verify the two halves of the ground truth agree: regression_gt.txt's case digest is an FNV-1a
  # fold of the per-game digests in gt_logs/<key>.wins, so a mismatch means this accept promoted a
  # fingerprint without its matching log (or vice versa). Checked HERE because an accept is the only
  # thing that writes either half, so it is the only place the drift can be introduced -- and 26
  # keys had silently drifted before this check existed. Advisory: report, do not fail the accept.
  if [ -f "$HERE/check_gt_logs.py" ] && command -v python3 >/dev/null 2>&1; then
    if ! python3 "$HERE/check_gt_logs.py" --mode "$MODE"; then
      echo "WARNING: the accepted $MODE ground truth is INTERNALLY INCONSISTENT (see above)."
      echo "         Every later run's per-game audit will mis-attribute those keys."
    fi
  fi
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

# Viewer LINE-BUILD checks (play-GUI frontend) remain standalone/on-demand -- run
# `bash test/viewer_checks.sh` after touching tools/play/. The engine<->GUI PROTOCOL layer,
# however, is now ALSO a regression gate (see "reference reproducibility" after the batch below):
# since the switch to threaded intent replay (docs/design/reference-intent-replay.md) the full
# 138-ref sweep costs seconds, and its --strict verdict is real signal -- play-drift/ENUM-GAP mean
# the engine moved under a recorded human game. It runs AFTER the deck batch so the GT
# fingerprints always land first (the historical hang concern), and it uses the same snapshot
# binary as the batch.

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
    # value_play integration (docs/design/value-leaf-fallback-table.md): every deck ships an ENABLED value_play
    # block that OWNS the play depth. The depth-5 case is the "value_play-driven" case -- we DROP the depth key
    # so the block decides the depth (burn=6, all others=5) and KEEP budget_ms as the resource knob (gate=20 ==
    # block default => byte-identical for the d5 decks; overnight keeps its generous per-deck budget, e.g.
    # burn/th=80, so burn runs d6 under deep search). The d0/d3 coverage cases PIN their explicit shallow depth,
    # which conflicts with the enabled block's depth lock, so they carry ignore_play_profile:true to bypass it.
    if [ "$depth" -eq 5 ]; then
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s, "weight": %s }' \
        "$name" "$file" "$prof" "$games" "$seed" "$bud" "$weight"
    else
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "depth": %s, "budget_ms": %s, "ignore_play_profile": true, "weight": %s }' \
        "$name" "$file" "$prof" "$games" "$seed" "$depth" "$bud" "$weight"
    fi
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

# Parse one "<name>: played=P avg=A digest=D" line per job into the avg/digest fingerprint,
# in matrix order, and compare to ground truth. avg = mean turn-to-win with an unwon game scored
# max_turns+1 (the goldfish metric); win/loss is intentionally NOT reported (see ComputeAvgTurns /
# main.cpp) because a goldfishing loss is an arbitrary horizon threshold agents wrongly prioritize.
CUR_DECK=""
for spec in "${CASES[@]}"; do
  # shellcheck disable=SC2086
  set -- $spec; deck=$1; depth=$2; seed=$3
  if [ "$deck" != "$CUR_DECK" ]; then CUR_DECK="$deck"; log ""; log "-- $CUR_DECK --"; fi
  key="${deck}_${MODE}_d${depth}_s${seed}"
  line=$(printf '%s\n' "$BATCH_OUT" | grep "^${key}: ")
  avg=$(printf '%s\n' "$line" | sed -nE 's/.*[^a-z]avg=([0-9.]+).*/\1/p')
  dg=$(printf '%s\n' "$line" | sed -nE 's/.*digest=([0-9a-f]+).*/\1/p')
  expected="${!key-}"
  if [ -z "$avg" ]; then
    status="FAIL"; got="(no output)"; FAIL=$((FAIL+1))
  else
    # Fingerprint = avg/play-digest. avg (to 4 dp) catches outcome/turn shifts; the digest catches a
    # play change that keeps the same avg. Backward-compat: a GT without a digest matches on avg alone.
    got="${avg}${dg:+/$dg}"
    echo "$key=$got" >> "$RESULTS"           # record for a later --accept
    if [ -z "$expected" ]; then
      status="NEW "; expected="<none>"; NEW=$((NEW+1))
    elif [ "$expected" = "$got" ]; then
      status="PASS"; PASS=$((PASS+1))
    elif [ "$expected" = "${avg}" ]; then
      status="PASS"; PASS=$((PASS+1))        # GT without a digest: avg matches
    else
      status="FAIL"; FAIL=$((FAIL+1))
    fi
  fi
  log "$(printf '  %s  %-26s exp=%-16s got=%-16s' "$status" "$key" "$expected" "$got")"

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

# ---- reference reproducibility (engine<->GUI protocol layer) ------------------------------
# Replays every saved references/<deck>/claude_*.json by INTENT (content-anchored picks, engine
# defaults for predated decision points -- docs/design/reference-intent-replay.md) against the
# SAME snapshot binary as the batch. --strict fails on play-drift (a recorded human line now ends
# differently) and ENUM-GAP (a previously-offered plan vanished for an identical state); the
# accepted classes (shuffle-dead, mull-drift) never gate. Threaded: the full sweep is seconds.
#
# REGRESSION MODE ONLY (USER, 2026-08-17: "I only want it to run in one mode. maybe full regression
# to be safe"). It is a whole-corpus REPLAY, not a sampled batch, so running it in all three modes
# re-measured the identical thing three times -- and each replay is a --claude-play process with
# MTG_UNPRUNED set, i.e. the widest enumeration the engine ever performs (that is what OOM'd the
# box; see docs/design/claude-play-unprune-blowup.md). Regression is the middle tier every real
# change goes through, so the gate keeps its coverage: smoke stays a fast fingerprint check, and
# overnight stops spending its concurrency on a sweep regression already ran. Force it in any mode
# with VPC_ALWAYS=1; skip it entirely with VPC_SKIP=1.
VPC="$HERE/viewer_protocol_check.py"
VPC_RUN=0
[ "$MODE" = regression ] && VPC_RUN=1
[ "${VPC_ALWAYS:-0}" = 1 ] && VPC_RUN=1
[ "${VPC_SKIP:-0}" = 1 ] && VPC_RUN=0
if [ "$VPC_RUN" != 1 ]; then
  log ""
  log "--- reference reproducibility: SKIPPED ($MODE; runs in regression mode -- VPC_ALWAYS=1 to force) ---"
elif [ -f "$VPC" ] && command -v python3 >/dev/null 2>&1; then
  log ""
  log "--- reference reproducibility (viewer protocol, --strict) ---"
  VPC_THREADS=$THREADS; [ "$VPC_THREADS" -le 0 ] && VPC_THREADS=$(nproc 2>/dev/null || echo 8)
  vpc_out=$(MTG_BIN="$BIN" python3 "$VPC" --strict --threads "$VPC_THREADS" 2>&1); vpc_rc=$?
  printf '%s\n' "$vpc_out" >> "$OUT"
  # Console gets the tally + any gating lines; the full per-ref detail lives in $OUT.
  printf '%s\n' "$vpc_out" | grep -E '^(Viewer protocol:|  (CONTRACT-FAIL|play-drift|ENUM-GAP))' \
    | while IFS= read -r vl; do echo "$vl"; done
  if [ $vpc_rc -ne 0 ]; then
    log "FAIL: reference reproducibility (--strict) -- a recorded human game no longer replays; see $OUT"
    FAIL=$((FAIL+1))
  fi
fi

# ---- per-game audit (split by depth) -- makes the pre-accept analysis unmissable ----------
# The fingerprint compare above governs PASS/FAIL; this appends the per-game breakdown the aggregate
# hides -- every SLOWER game (worse loss-penalized score; a win becoming unwon is just the maximal
# slowdown, not a special category) split searched vs d0 -- so an ordinary run already surfaces what to analyze
# before --accept. The metric is the loss-penalized avg (loss = max_turns+1); the accept decision is a
# human judgement on the NET delta, not a per-game gate. See docs/design/auto-audit-integration.md.
if [ -f "$HERE/audit_changed_games.py" ] && command -v python3 >/dev/null 2>&1; then
  log ""
  # The audit below diffs against test/gt_logs/. Those per-game logs and the fingerprints in
  # regression_gt.txt are promoted by the same --accept but under different rules, so they CAN
  # drift apart (a per-deck accept, a clobbered wins dir, an interrupted run) -- and a stale
  # per-game log makes every later run's audit report a PREVIOUS commit's changes as its own.
  # That is not hypothetical: on 2026-08-24 a green 42/42 smoke run simultaneously reported 2
  # slower + 22 play-changed games, all of them eaccc120's, on decks the change under test could
  # not touch. Say so before printing an audit the reader would otherwise trust.
  if [ -f "$HERE/check_gt_logs.py" ]; then
    if ! gtchk=$(python3 "$HERE/check_gt_logs.py" --mode "$MODE" 2>&1); then
      log "--- WARNING: committed gt_logs are STALE vs regression_gt.txt ---"
      printf '%s\n' "$gtchk" | grep -E '^(STALE|---)' | while IFS= read -r cl; do log "  $cl"; done
      log "  The per-game audit below is diffing against those stale logs -- treat its"
      log "  'slower / play-changed' list as UNATTRIBUTED until a clean run is accepted."
    fi
  fi
  log "--- per-game audit (vs committed gt_logs) ---"
  audit_out=$(python3 "$HERE/audit_changed_games.py" "$MODE" 2>&1)
  printf '%s\n' "$audit_out" | while IFS= read -r al; do log "$al"; done
  # Offer the churn auto-classifier when there are searched SLOWER games to explain.
  if printf '%s\n' "$audit_out" | grep -q "SEARCHED-depth SLOWER"; then
    log "      >> classify searched slower games automatically: bash test/classify_turn_later.sh $MODE"
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
