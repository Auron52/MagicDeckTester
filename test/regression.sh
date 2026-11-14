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
#   bash test/regression.sh --smoke --regression   # (or --both) BOTH tiers in ONE pooled batch; each
#                                                  # tier still reports/records/fails on its own keys
#
# Wall time per stage is stamped as "[stage] <name> <s>" lines (console + each tier's result file).
# The reference stages (regression tier) run CONCURRENTLY with the game batch on one shared token
# budget (MTG_JOBSERVER); SIDE_STAGES_SERIAL=1 runs them after the batch, the pre-2026-11-14 order.
#
# Update ground truth from a run you have inspected and ACCEPT (no re-run):
#   bash test/regression.sh --smoke --accept     # promote last smoke results
#   bash test/regression.sh --both --accept      # promote BOTH tiers' last results (one accept each)
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
GT=${REGRESSION_GT:-test/regression_gt.txt}
GTLOGS=${REGRESSION_GT_LOGS:-test/gt_logs}
THREADS=${THREADS:-0}

MODE=regression
MODES=()          # every tier flag given, in order: >1 runs them as ONE pooled batch (see "COMBINED" below)
ACCEPT=0
ACCEPT_ACK=""     # --accept-with-regressions=<note>: same as --accept (promote, no re-run/re-audit);
                  # the note is recorded in the GT provenance header to document an intended slowdown.
DECK_ONLY=""      # --deck=<name>: restrict this run to one deck's cases (see regression_cases.sh)
for arg in "$@"; do
  case "$arg" in
    --smoke)     MODES+=(smoke) ;;
    --regression) MODES+=(regression) ;;   # the default; accepted explicitly so a scripted sweep can
                                           # pass a mode flag UNIFORMLY instead of special-casing ""
    --overnight) MODES+=(overnight) ;;
    --fast)      MODES+=(smoke) ;;      # back-compat alias
    --both)      MODES+=(smoke regression) ;;   # = --smoke --regression (one pooled batch)
    --accept)    ACCEPT=1 ;;
    --accept-with-regressions=*) ACCEPT=1; ACCEPT_ACK="${arg#*=}" ;;
    --deck=*)    DECK_ONLY="${arg#*=}" ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done
# De-duplicate, keep the order given; none = regression (the historical default).
_m=(); for _x in "${MODES[@]}"; do [[ " ${_m[*]} " == *" $_x "* ]] || _m+=("$_x"); done; MODES=("${_m[@]}")
[ ${#MODES[@]} -eq 0 ] && MODES=(regression)
MODE=${MODES[0]}

# COMBINED TIERS (USER 2026-11-14: "All things that can be should be combined into one batch. This even
# includes the possibility to run smoke and regression full together when we intend to."). Several tier
# flags (`--smoke --regression`, or `--both`) run EVERY game of EVERY named tier as ONE `mtg --batch`,
# so the run pays one load-imbalance tail instead of one per tier; each tier then reports, records and
# fails exactly as its own run would (its own test/logs/<mode>/, results env, result file and per-game
# diff). An --accept with several tiers accepts each in turn -- the same as one --accept per tier.
if [ "$ACCEPT" = 1 ] && [ ${#MODES[@]} -gt 1 ]; then
  _rc=0
  for _x in "${MODES[@]}"; do
    _a=("--$_x" --accept)
    [ -n "$ACCEPT_ACK" ] && _a=("--$_x" "--accept-with-regressions=$ACCEPT_ACK")
    [ -n "$DECK_ONLY" ] && _a+=("--deck=$DECK_ONLY")
    bash "${BASH_SOURCE[0]}" "${_a[@]}" || _rc=1
  done
  exit $_rc
fi

# shellcheck source=regression_cases.sh
source "$HERE/regression_cases.sh"

# select_mode <mode>: point every per-tier global (LOGDIR, OUT, RESULTS, MODEFLAG, CASES) at one tier.
# Called once for the tier being accepted / run, and per tier by a combined run (each tier keeps
# exactly the files, keys and case list its own run would have).
select_mode() {
MODE=$1
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

}
select_mode "$MODE"

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
  if [ -n "$DECK_ONLY" ]; then
    # A FILTERED accept overrides ONLY the filtered decks' keys. $RESULTS can come from a FULL run
    # (it then holds every key of the mode); sourcing it whole promoted every deck's aggregate while
    # the .wins loop below promoted only the filtered decks' logs, splitting the two GT halves
    # (2026-10-20, `--accept --deck=alchemist` after a full regression run: 17 stale keys).
    for spec in "${CASES[@]}"; do
      # shellcheck disable=SC2086
      set -- $spec
      _key="${1}_${MODE}_d${2}_s${3}"
      _line="$(grep -E "^${_key}=" "$RESULTS" | tail -1)"
      [ -n "$_line" ] && printf -v "$_key" '%s' "${_line#*=}"
    done
  else
    # shellcheck disable=SC1090
    source "$RESULTS"                                 # override the accepted mode
  fi
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
  #
  # ...but ONLY WHEN THIS RUN ACTUALLY SUPERSEDES IT, i.e. a FULL-tier accept. A --deck=<...>
  # accept supersedes nothing: it re-measures a handful of one deck's keys and leaves every other
  # key of that mode -- including all the ones the note explains -- carried on the note's basis.
  # Dropping it there is the SAME provenance loss described above, one level deeper, and it is
  # worse than the 2026-08-29 case because the erasing run need not touch a single cell the note
  # covers. It fired on 2026-09-20: adding the 4 new angels2hg overnight keys under
  # --deck=angels2hg erased the 2026-09-19 note documenting 52 unrelated overnight cells accepted
  # stale from 56c57d73. Caught only by diffing the GT file before and after the accept.
  # A fresh --accept-with-regressions for this mode DOES supersede, filtered or not, so the drop
  # still applies whenever ACCEPT_ACK is being written.
  # A filtered accept never supersedes the mode's note WHOLESALE even when it writes its own: the
  # two cover different decks, and both sets of cells are still carried on their own note. So the
  # carry-all branch is NOT conditioned on ACCEPT_ACK being empty -- scope a per-deck note in its
  # own text instead. (Caught immediately: adopting the Angels leafless shape needed an ack, which
  # under the ACCEPT_ACK-gated form would have re-erased the 2026-09-19 note this very guard was
  # added to protect.)
  prior_acks=""
  if [ -f "$GT" ] && [ -n "$DECK_ONLY" ]; then
    prior_acks="$(grep -E '^# accepted-with-regressions \(' "$GT" 2>/dev/null || true)"
  elif [ -f "$GT" ]; then
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
  mkdir -p "$GTLOGS"
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
      cp "$LOGDIR/wins/${key}.wins" "$GTLOGS/${key}.wins"; promoted=$((promoted+1))
    fi
  done
  # Save the binary that produced these accepted results as the per-mode BASELINE, so the next
  # run's audit can diff current-vs-baseline per game (explain_game.py) with no rebuild. The run
  # snapshotted its exact binary to $LOGDIR/mtg.run (dirty-state-safe); it now IS the baseline.
  # Under logs/snapshots/ (gitignored): a fresh clone lacks it and explain_game falls back to the
  # NEW-line-only view until the first local --accept re-creates it.
  if [ -f "$LOGDIR/mtg.run" ]; then
    mkdir -p logs/snapshots
    harness_install_bin "$LOGDIR/mtg.run" "logs/snapshots/${MODE}-baseline"
    [ -f "$LOGDIR/mtg.run.meta" ] && cp -f "$LOGDIR/mtg.run.meta" "logs/snapshots/${MODE}-baseline.meta"
    [ -f "$LOGDIR/mtg.run.diff" ] && cp -f "$LOGDIR/mtg.run.diff" "logs/snapshots/${MODE}-baseline.diff"
    echo "Saved baseline binary -> logs/snapshots/${MODE}-baseline (for next run's per-game diff)."
  fi

  echo "Accepted $MODE results into $GT (and $promoted per-game log(s) into $GTLOGS/)."
  # Record the accepted jobs' measured cost for the next run's scheduling (see JOB_COST_TABLE).
  if [ -f "$LOGDIR/batch.log" ] && command -v python3 >/dev/null 2>&1; then
    python3 "$HERE/lib/job_cost.py" update "$HERE/job_cost.tsv" "$LOGDIR/batch.log" "$RESULTS"
  fi
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

# ---- run the tier(s) and compare ------------------------------------------
# THE ENGINE (user, 2026-10-08: PGO+LTO "for everything except maybe quick development cycles").
# harness_bin above already takes build/PGO when it was built from exactly this HEAD:src with a clean
# src/ (byte-identical to Release, ~1.3x faster), so smoke and regression use it for free and otherwise
# run Release with no build delay. The OVERNIGHT tier is long enough that it builds a stale PGO first.
# MTG_PGO=0 pins Release (and an explicit MTG_BIN always wins).
if [[ " ${MODES[*]} " == *" overnight "* ]] && [ -z "${MTG_BIN:-}" ]; then
  harness_pgo_ensure
  BIN=$(harness_bin) || exit 1
fi
BIN_KIND=$(harness_bin_kind "$BIN")
if [ ! -f "$BIN" ]; then
  echo "ERROR: $BIN not found. Run cmake --build build --config Release first." >&2
  exit 1
fi

# STAGE WALL TIMES (2026-11-14). Every stage this harness runs stamps its own wall time, live on the
# console and again in each tier's result file just before its Result line, so "where did the tier's
# time go" is read off the log and never guessed. The USER's bar: the side stages "should be a fraction
# at most" of the tier. A stage that runs CONCURRENTLY with the game batch (below) reports its own
# start-to-finish wall, so the stage lines can sum to more than the tier's wall.
STAGE_LINES=()
stage_line() {   # <name> <seconds> [note]
  local l
  l=$(printf '[stage] %-40s %6ss = %dm%02ds%s' "$1" "$2" $(($2/60)) $(($2%60)) "${3:+  $3}")
  echo "$l"; STAGE_LINES+=("$l")
}
RUN_T0=$(date +%s)

# Snapshot the binary into the run dir and execute the COPY. A long run can then
# overlap with rebuilds of build/Release/mtg (iterating on other work) without
# hitting ETXTBSY ("Text file busy", which the linker raises when it opens a running
# executable for write) or silently swapping the binary mid-run. The snapshot is
# what every batch invocation below uses; the source binary is free to be rebuilt.
# A combined run snapshots into EVERY tier's dir (each tier's --accept saves its own mtg.run as that
# tier's baseline) and executes the first tier's copy -- the bytes are the same.
SRC_BIN="$BIN"
RUN_BIN=""
OUTS=()
for _mode in "${MODES[@]}"; do
  select_mode "$_mode"
  BIN_SNAPSHOT="$LOGDIR/mtg.run"
  harness_install_bin "$SRC_BIN" "$BIN_SNAPSHOT"   # in the devcontainer: bytes on the build volume, never run off 9p
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
    echo "binary_kind=$BIN_KIND"
  } > "$BIN_SNAPSHOT.meta" 2>/dev/null || true
  [ -z "$RUN_BIN" ] && RUN_BIN="$BIN_SNAPSHOT"
  OUTS+=("$OUT")
done
BIN="$RUN_BIN"

[ -f "$GT" ] && source "$GT" 2>/dev/null || true

log() { echo "$1"; echo "$1" >> "$OUT"; }
# Into EVERY tier's result file (console once): the stages a combined run shares.
log_all() { local o; echo "$1"; for o in "${OUTS[@]}"; do echo "$1" >> "$o"; done; }

for _mode in "${MODES[@]}"; do
  select_mode "$_mode"
  : > "$OUT"
  : > "$RESULTS"
  log "=== REGRESSION ($MODE) $(date) ==="
  log "(threads=$THREADS; binary=$BIN [$BIN_KIND]; logs in $LOGDIR)"
  [ ${#MODES[@]} -gt 1 ] && log "(COMBINED run: tiers ${MODES[*]} share ONE pooled batch -- see test/logs/combined/)"
done

# Scenario sanity gate: hand-built board fixtures (test/scenarios/*.json) that assert a specific
# interaction still plays correctly. A FAIL is a hard regression and fails the tier. The fixtures run in
# PARALLEL, longest first (test/scenarios.sh), and -- where the jobserver below exists -- CONCURRENTLY
# with the game batch on the shared tokens, so they cost no wall of their own: a FAIL kills the batch at
# once (nothing it would measure can be accepted) and the tier aborts. Without the jobserver
# (SIDE_STAGES_SERIAL=1, no mkfifo) they run up front, before the batch, as they always did.
SCEN="$(dirname "$0")/scenarios.sh"
[ -f "$SCEN" ] || SCEN=""
scen_abort() {
  log_all "ABORT: scenario sanity failed (a hand-built fixture regressed) -- fix before running the batch."
  exit 1
}

# Viewer LINE-BUILD checks (play-GUI frontend) remain standalone/on-demand -- run
# `bash test/viewer_checks.sh` after touching tools/play/. The engine<->GUI PROTOCOL layer,
# however, is now ALSO a regression gate (see "reference reproducibility" below): since the switch
# to threaded intent replay (docs/design/reference-intent-replay.md) its --strict verdict is real
# signal -- play-drift/ENUM-GAP mean the engine moved under a recorded human game. It uses the same
# snapshot binary as the batch, and since 2026-11-14 it runs CONCURRENTLY with the batch (see
# "SIDE STAGES" below); its verdict is still reported after the GT fingerprints.

# Emit the whole case matrix as one batch manifest. `mtg.exe --batch` pools every
# game of every case into a single atomic work queue, so the suite pays ONE
# load-imbalance tail instead of one per case (the old per-case sweep stranded a
# core on each config's slowest game). Results are validated byte-identical to the
# per-case runs, so the ground truth is unchanged. See docs/design/batch-runner.md.
# MEASURED JOB COSTS (test/job_cost.tsv, written by --accept via test/lib/job_cost.py): each known job's
# work units per game becomes its LPT weight, so the expensive chunks (NivMizzetCombo / Bruna d5 in
# smoke) are dispatched FIRST and the cheap ones backfill -- USER 2026-10-23, after smoke's makespan sat
# at 13m15s for ~6.5 min of pooled work. A job with no measurement keeps the rules below. Scheduling
# only: every game is seeded by its job + index, so order never changes a result.
JOB_COST_TABLE="$HERE/job_cost.tsv"
declare -A JOB_COST_U=()
if [ -f "$JOB_COST_TABLE" ] && command -v python3 >/dev/null 2>&1; then
  while read -r _jk _ju; do JOB_COST_U[$_jk]=$_ju; done < <(python3 "$HERE/lib/job_cost.py" emit "$JOB_COST_TABLE")
fi
# emit_jobs: the current tier's job objects, ",\n"-joined, no trailing newline (the manifest body).
emit_jobs() {
  local first=1 spec deck depth seed games budget base extra2hg file prof name bud weight
  for spec in "${CASES[@]}"; do
    # shellcheck disable=SC2086
    set -- $spec; deck=$1; depth=$2; seed=$3; games=$4; budget=$5
    # 2HG VARIANT cases: a "<deck>2hg" case key runs the SAME deck/profile (user decision
    # 2026-09-04: 2HG shares the shipped mulligan profile / value leaf -- no per-2HG artifacts)
    # at Two-Headed Giant settings: starting_life 30 + opponent_heads 2 (core/GameSetup.h).
    # The suffix stays in the GT key, so hinata2hg_* and hinata_* are separate ground truths.
    base=$deck; extra2hg=""
    if [[ "$deck" == *2hg ]]; then
      base=${deck%2hg}
      extra2hg='"starting_life": 30, "opponent_heads": 2, '
    fi
    file=${DECK_FILE[$base]}; prof=${DECK_PROF[$base]}
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
    if [ "$base" = "hinata" ] && [ "$depth" -gt 0 ]; then weight=$((depth * 1000 + bud)); fi
    # UNBUDGETED BACKSTOP rows (searched depth, budget 0 = unlimited; see the note above them in
    # regression_cases.sh). They were screened to games far below this ceiling, so it never binds on a
    # healthy engine; it exists so a change that makes one of them DEGENERATE abandons that game
    # deterministically (the case's avg/digest then moves and the tier FAILS) instead of hanging the
    # tier for hours. 2,000,000 units is ~45 s of search on this box. Weight 1 starts them ahead of
    # every weight-0 job: by the batch's own depth/budget order budget 0 sorts LAST at its depth, which
    # would put unbounded games on the tail (Hinata's weight, if any, still outranks them).
    if [ "$depth" -gt 0 ] && [ "$bud" -eq 0 ]; then
      extra2hg="${extra2hg}\"abandon_units\": 2000000, "
      [ "$weight" -eq 0 ] && weight=1
    fi
    # A measured cost wins over both rules above (see JOB_COST_TABLE).
    [ -n "${JOB_COST_U[$name]-}" ] && weight=${JOB_COST_U[$name]}
    [ $first -eq 1 ] && first=0 || printf ',\n'
    # value_play integration (docs/design/value-leaf-fallback-table.md): every deck ships an ENABLED value_play
    # block that OWNS the play depth. The depth-5 case is the "value_play-driven" case -- we DROP the depth key
    # so the block decides the depth (burn=6, all others=5) and KEEP budget_ms as the resource knob (gate=20 ==
    # block default => byte-identical for the d5 decks; overnight keeps its generous per-deck budget, e.g.
    # burn/th=80, so burn runs d6 under deep search). The d0/d3 coverage cases PIN their explicit shallow depth,
    # which conflicts with the enabled block's depth lock, so they carry ignore_play_profile:true to bypass it.
    if [ "$depth" -eq 5 ]; then
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", %s"games": %s, "seed": %s, "budget_ms": %s, "weight": %s }' \
        "$name" "$file" "$prof" "$extra2hg" "$games" "$seed" "$bud" "$weight"
    else
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", %s"games": %s, "seed": %s, "depth": %s, "budget_ms": %s, "ignore_play_profile": true, "weight": %s }' \
        "$name" "$file" "$prof" "$extra2hg" "$games" "$seed" "$depth" "$bud" "$weight"
    fi
  done
}
# Each tier's own manifest (byte-identical to a single-tier run's), and the batch's: the tier's own
# for a single tier, every tier's jobs concatenated for a combined run.
if [ ${#MODES[@]} -gt 1 ]; then BDIR=test/logs/combined; else BDIR=$LOGDIR; fi
mkdir -p "$BDIR"
MANIFEST="$BDIR/manifest.json"
_all_jobs=""
for _mode in "${MODES[@]}"; do
  select_mode "$_mode"
  # Play rows test what SHIPS (USER 2026-10-27): warn when one's budget drifted from the deck's value_play budget.
  if command -v python3 >/dev/null 2>&1; then
    _profs=(); for _k in "${!DECK_PROF[@]}"; do _profs+=("$_k=${DECK_PROF[$_k]}"); done
    printf '%s\n' "${CASES[@]}" | python3 "$HERE/lib/play_rows_check.py" "${_profs[@]}"
  fi
  _jobs=$(emit_jobs)
  printf '{ "jobs": [\n%s\n] }\n' "$_jobs" > "$LOGDIR/manifest.json"
  _all_jobs="${_all_jobs:+$_all_jobs,
}$_jobs"
done
[ ${#MODES[@]} -gt 1 ] && printf '{ "jobs": [\n%s\n] }\n' "$_all_jobs" > "$MANIFEST"

# ---- SIDE STAGES: reference replays + suboptimal require-beat, CONCURRENT with the batch ------------
# Replays every saved references/<deck>/claude_*.json by INTENT (content-anchored picks, engine
# defaults for predated decision points -- docs/design/reference-intent-replay.md) against the
# SAME snapshot binary as the batch. --strict fails on play-drift (a recorded human line now ends
# differently) and ENUM-GAP (a previously-offered plan vanished for an identical state); the
# accepted classes (shuffle-dead, mull-drift) never gate.
#
# REGRESSION MODE ONLY (USER, 2026-08-17: "I only want it to run in one mode. maybe full regression
# to be safe"). It is a whole-corpus REPLAY, not a sampled batch, so running it in all three modes
# re-measured the identical thing three times -- and each replay is a --claude-play process with
# MTG_UNPRUNED set, i.e. the widest enumeration the engine ever performs (that is what OOM'd the
# box; see docs/design/claude-play-unprune-blowup.md). Regression is the middle tier every real
# change goes through, so the gate keeps its coverage: smoke stays a fast fingerprint check, and
# overnight stops spending its concurrency on a sweep regression already ran. Force it in any mode
# with VPC_ALWAYS=1; skip it entirely with VPC_SKIP=1. A combined run replays the corpus ONCE and
# reports it under every tier that would have run it.
#
# CONCURRENT, ON ONE THREAD BUDGET (USER 2026-11-14: "All things that can be should be combined into one
# batch"). These two stages used to run AFTER the game batch, serially: on 2026-11-14 the reference
# sweep alone was ~28 min of a ~61 min regression tier, most of it one ~26-minute reference running on
# an otherwise idle box. A reference walk cannot be a batch job (it is a chain of engine calls whose
# every pick depends on the previous frame), so instead the sweep, ref_bench's own pooled batch and the
# tier batch all draw from ONE jobserver FIFO of $T tokens (MTG_JOBSERVER; src/runner/BatchRunner.cpp
# JobServer, viewer_protocol_check._timed_check): at most $T games/walks run at once across all three
# processes (total threads <= cores), and whichever has work left absorbs the tokens the others stop
# using. Scheduling only -- every verdict and digest is what the serial order produced. The sweep
# itself replays each reference through ONE persistent --interactive child instead of re-simulating the
# whole pick prefix per decision (viewer_protocol_check INTERACTIVE REPLAY), which is most of its cost.
# Without mkfifo (Windows), or with SIDE_STAGES_SERIAL=1, they run after the batch as before.
VPC="$HERE/viewer_protocol_check.py"
VPC_ANY=0
vpc_wanted() {   # does tier $1 run the reference stages?
  local r=0
  [ "$1" = regression ] && r=1
  [ "${VPC_ALWAYS:-0}" = 1 ] && r=1
  [ "${VPC_SKIP:-0}" = 1 ] && r=0
  [ $r = 1 ]
}
for _mode in "${MODES[@]}"; do vpc_wanted "$_mode" && VPC_ANY=1; done
{ [ -f "$VPC" ] && command -v python3 >/dev/null 2>&1; } || VPC_ANY=0
T=$THREADS; [ "$T" -le 0 ] && T=$(nproc 2>/dev/null || echo 8)
SIDE_DIR="$BDIR/side"
JS_FIFO=""
if { [ "$VPC_ANY" = 1 ] || [ -n "$SCEN" ]; } && [ "${SIDE_STAGES_SERIAL:-0}" != 1 ] && command -v mkfifo >/dev/null 2>&1; then
  JS_DIR=$(mktemp -d "${TMPDIR:-/tmp}/mtg-jobserver.XXXXXX") && mkfifo "$JS_DIR/fifo" \
    && exec {JS_FD}<>"$JS_DIR/fifo" && JS_FIFO="$JS_DIR/fifo"
  if [ -n "$JS_FIFO" ]; then
    printf "%${T}s" "" | tr ' ' '+' >&"$JS_FD"
    trap 'exec {JS_FD}>&-; rm -f "$JS_FIFO"; rmdir "$JS_DIR" 2>/dev/null' EXIT
  fi
fi
# start_side_stages: launch both stages in the background; each writes <name>.out/.rc/.t0/.t1.
start_side_stages() {
  mkdir -p "$SIDE_DIR"
  local f
  for f in vpc subopt; do : > "$SIDE_DIR/$f.out"; printf '' > "$SIDE_DIR/$f.rc"; done
  (
    date +%s > "$SIDE_DIR/vpc.t0"
    MTG_JOBSERVER="$JS_FIFO" MTG_BIN="$BIN" python3 "$VPC" --strict --threads "$T" > "$SIDE_DIR/vpc.out" 2>&1
    echo $? > "$SIDE_DIR/vpc.rc"; date +%s > "$SIDE_DIR/vpc.t1"
  ) &
  VPC_PID=$!
  # SUBOPTIMAL references must be STRICTLY BEATEN by the search (USER 2026-10-22: "Suboptimal games
  # could be replayed as well with the expectation that we strictly beat them"). The sweep replays
  # their recorded lines; this plays each from its shuffle at the deck's committed play setting and
  # fails unless the search wins EARLIER than the user did. A suboptimal game with a proper reference
  # for the same game is superseded and skipped. A handful of games, in ref_bench's own pooled batch
  # (on the shared tokens).
  (
    date +%s > "$SIDE_DIR/subopt.t0"
    MTG_JOBSERVER="$JS_FIFO" MTG_BIN="$BIN" python3 "$HERE/../scripts/ref_bench.py" --suboptimal --require-beat \
      > "$SIDE_DIR/subopt.out" 2>&1
    echo $? > "$SIDE_DIR/subopt.rc"; date +%s > "$SIDE_DIR/subopt.t1"
  ) &
  SUB_PID=$!
}
wait_side_stages() {
  wait "$VPC_PID" "$SUB_PID"
  stage_line "reference replays (viewer protocol)" $(( $(cat "$SIDE_DIR/vpc.t1") - $(cat "$SIDE_DIR/vpc.t0") )) \
    "${JS_FIFO:+(concurrent with the batch, shared tokens)}"
  stage_line "suboptimal require-beat (ref_bench)" $(( $(cat "$SIDE_DIR/subopt.t1") - $(cat "$SIDE_DIR/subopt.t0") )) \
    "${JS_FIFO:+(concurrent with the batch, shared tokens)}"
}
[ "$VPC_ANY" = 1 ] && [ -n "$JS_FIFO" ] && start_side_stages

mkdir -p "$SIDE_DIR"
if [ -n "$SCEN" ] && [ -z "$JS_FIFO" ]; then
  log_all "--- scenario sanity ---"
  _t0=$(date +%s)
  if THREADS=$THREADS MTG_BIN="$BIN" bash "$SCEN" | tee -a "${OUTS[@]}"; then :; else scen_abort; fi
  stage_line "scenario sanity" $(( $(date +%s) - _t0 ))
elif [ -n "$SCEN" ]; then
  # Concurrent: one fixture per token. On a FAIL, kill the batch (its pid is written by the wrapper below).
  : > "$SIDE_DIR/batch.pid"; printf '' > "$SIDE_DIR/scen.rc"
  (
    date +%s > "$SIDE_DIR/scen.t0"
    THREADS=$THREADS MTG_JOBSERVER="$JS_FIFO" MTG_BIN="$BIN" bash "$SCEN" > "$SIDE_DIR/scen.out" 2>&1
    rc=$?; echo $rc > "$SIDE_DIR/scen.rc"; date +%s > "$SIDE_DIR/scen.t1"
    if [ $rc != 0 ]; then
      for _i in 1 2 3 4 5 6 7 8 9 10; do [ -s "$SIDE_DIR/batch.pid" ] && break; sleep 1; done
      _bp=$(cat "$SIDE_DIR/batch.pid" 2>/dev/null)
      [ -n "$_bp" ] && kill "$_bp" 2>/dev/null
    fi
  ) &
  SCEN_PID=$!
fi

TOTAL_START=$(date +%s)
# `tee` to batch.log so per-job results STREAM to the terminal live as each case
# finishes (the binary flushes one line per completed job), instead of being buffered
# inside $() until the whole batch ends. We then read the captured file for parsing.
# Keep an independent copy of the streamed batch output. A container cost run
# once finished with its live batch.log truncated to zero after the last job;
# without this copy every result reads as "no output" and the cost gate loses a
# completed 12-minute measurement. The backup has a unique name so another tier
# process cannot clobber it; remove it after verifying the public log.
BATCH_BACKUP="$BDIR/batch.$$.backup"
if [ ${#MODES[@]} -gt 1 ]; then
  # The combined batch writes per-game logs here and they are MOVED into each tier's own wins dir
  # below; clear what an interrupted earlier combined run may have left, so nothing stale is moved.
  mkdir -p "$BDIR/wins"; find "$BDIR/wins" -maxdepth 1 -type f -delete
fi
( echo $BASHPID > "$SIDE_DIR/batch.pid"; MTG_JOBSERVER="$JS_FIFO" exec "$BIN" --batch "$MANIFEST" --threads "$THREADS" --game-log-dir "$BDIR/wins" ) \
    2>"$BDIR/batch.err" | tee "$BDIR/batch.log" "$BATCH_BACKUP"
if ! cmp -s "$BDIR/batch.log" "$BATCH_BACKUP"; then
  cp "$BATCH_BACKUP" "$BDIR/batch.log"
  echo "Restored $BDIR/batch.log from the independent batch stream" >&2
fi
rm -f "$BATCH_BACKUP"
TOTAL=$(( $(date +%s) - TOTAL_START ))
stage_line "game batch (mtg --batch)" "$TOTAL" "${JS_FIFO:+(shared tokens with the reference stages)}"
if [ -n "$SCEN" ] && [ -n "$JS_FIFO" ]; then
  wait "$SCEN_PID"
  log_all "--- scenario sanity ---"
  tee -a "${OUTS[@]}" < "$SIDE_DIR/scen.out"
  [ "$(cat "$SIDE_DIR/scen.rc")" = 0 ] || scen_abort
  stage_line "scenario sanity" $(( $(cat "$SIDE_DIR/scen.t1") - $(cat "$SIDE_DIR/scen.t0") )) \
    "(concurrent with the batch, shared tokens)"
fi

# A combined run hands each tier exactly the files its own run would have written: its job lines of
# batch.log (+ the header/footer), the shared batch.err, and its per-game logs (<key>.wins/.units/...).
if [ ${#MODES[@]} -gt 1 ]; then
  for _mode in "${MODES[@]}"; do
    select_mode "$_mode"
    mkdir -p "$LOGDIR/wins"
    _keys=$(for spec in "${CASES[@]}"; do set -- $spec; echo "${1}_${MODE}_d${2}_s${3}"; done)
    awk 'FNR==NR{k[$0]=1;next} /^[^ ]+: played=/{n=$1; sub(/:$/,"",n); if(!(n in k)) next} {print}' \
        <(printf '%s\n' "$_keys") "$BDIR/batch.log" > "$LOGDIR/batch.log"
    cp -f "$BDIR/batch.err" "$LOGDIR/batch.err"
    while IFS= read -r _k; do
      for _f in "$BDIR/wins/$_k".*; do [ -e "$_f" ] && mv -f "$_f" "$LOGDIR/wins/"; done
    done <<< "$_keys"
  done
fi

if [ "$VPC_ANY" = 1 ]; then
  if [ -z "$JS_FIFO" ]; then
    # Serial fallback (no FIFOs, or SIDE_STAGES_SERIAL=1): the historical order, after the batch.
    start_side_stages
  fi
  wait_side_stages
fi

# report_mode: everything a single tier's run reports after its games -- the fingerprint compare and
# per-game diff, the overflow gate, the reference verdicts, the per-game audit, the Result line.
# -> 0 when the tier passes.
report_mode() {
  PASS=0; FAIL=0; NEW=0
  local BATCH_OUT spec deck depth seed key line avg dg expected status got new_wins gt_wins diffs nch
  BATCH_OUT=$(cat "$LOGDIR/batch.log")

  # Per-game ground-truth logs live in test/gt_logs/<key>.wins (committed). The batch
  # run just wrote this run's per-game win turns to $LOGDIR/wins/<key>.wins. We diff
  # them so the report names EXACTLY which games changed -- the cheap built-in version
  # of the mandatory pre-accept per-game analysis (no rebuilding the old binary).

  # Parse one "<name>: played=P avg=A digest=D" line per job into the avg/digest fingerprint,
  # in matrix order, and compare to ground truth. avg = mean turn-to-win with an unwon game scored
  # max_turns+1 (the goldfish metric); win/loss is intentionally NOT reported (see ComputeAvgTurns /
  # main.cpp) because a goldfishing loss is an arbitrary horizon threshold agents wrongly prioritize.
  local CUR_DECK=""
  for spec in "${CASES[@]}"; do
    # shellcheck disable=SC2086
    set -- $spec; deck=$1; depth=$2; seed=$3
    if [ "$deck" != "$CUR_DECK" ]; then CUR_DECK="$deck"; log ""; log "-- $CUR_DECK --"; fi
    key="${deck}_${MODE}_d${depth}_s${seed}"
    line=$(printf '%s\n' "$BATCH_OUT" | grep "^${key}: ")
    # ANCHOR the extraction to THIS key and the exact field order. The old patterns led with a
    # greedy `.*`, so when two workers' result lines spliced together in the batch output (seen
    # 2026-09-08: "stompy...ms=216" + "fivecolour_overnight_d5_s4004: played=300 avg=4.8633...")
    # the greedy prefix skipped this case's own avg/digest and captured the SPLICED-IN case's --
    # reporting stompy as 4.5540 -> 4.8633, a 0.31-turn "regression" that never happened, and one
    # that --accept would have written into GT as a false fingerprint. Anchored, a malformed line
    # simply yields no avg and the case fails safe as "(no output)".
    avg=$(printf '%s\n' "$line" | sed -nE "s/^${key}: played=[0-9]+ avg=([0-9.]+) .*/\1/p")
    dg=$(printf '%s\n' "$line" | sed -nE "s/^${key}: played=[0-9]+ avg=[0-9.]+ digest=([0-9a-f]+)( .*)?$/\1/p")
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

  # ---- CounterList overflow (src/core/Permanent.h) -----------------------------------------------
  # The binary prints "[counterlist] OVERFLOW xN" at exit if a counter put ever fell past the inline
  # array. Storage is canonical (one entry per Counter::Type) so it cannot happen today; the line was
  # once printed x5,787,486 per regression batch for weeks with nothing reading it, because the
  # Permanent.h comment claimed a gate that did not exist. This is that gate.
  if grep -q '^\[counterlist\] OVERFLOW' "$LOGDIR/batch.err" 2>/dev/null; then
    log ""
    log "FAIL: $(grep -m1 '^\[counterlist\] OVERFLOW' "$LOGDIR/batch.err")"
    FAIL=$((FAIL+1))
  fi

  # ---- reference reproducibility (engine<->GUI protocol layer) ------------------------------
  # (Run as a SIDE STAGE above; its verdict is reported and gates here, in the tier that owns it.)
  if ! vpc_wanted "$MODE"; then
    log ""
    log "--- reference reproducibility: SKIPPED ($MODE; runs in regression mode -- VPC_ALWAYS=1 to force) ---"
  elif [ "$VPC_ANY" = 1 ]; then
    local vpc_out vpc_rc sub_out sub_rc
    log ""
    log "--- reference reproducibility (viewer protocol, --strict) ---"
    vpc_out=$(cat "$SIDE_DIR/vpc.out"); vpc_rc=$(cat "$SIDE_DIR/vpc.rc")
    printf '%s\n' "$vpc_out" >> "$OUT"
    # Console gets the tally + any gating lines; the full per-ref detail lives in $OUT.
    printf '%s\n' "$vpc_out" | grep -E '^(Viewer protocol:|  (CONTRACT-FAIL|play-drift|ENUM-GAP))' \
      | while IFS= read -r vl; do echo "$vl"; done
    if [ "$vpc_rc" != 0 ]; then
      log "FAIL: reference reproducibility (--strict) -- a recorded human game no longer replays; see $OUT"
      FAIL=$((FAIL+1))
    fi
    log ""
    log "--- suboptimal references (the search must strictly beat each one) ---"
    sub_out=$(cat "$SIDE_DIR/subopt.out"); sub_rc=$(cat "$SIDE_DIR/subopt.rc")
    printf '%s\n' "$sub_out" >> "$OUT"
    printf '%s\n' "$sub_out" | grep -E '^(REQUIRE-BEAT|  )' | while IFS= read -r sl; do echo "$sl"; done
    if [ "$sub_rc" != 0 ]; then
      log "FAIL: a suboptimal reference is no longer strictly beaten by the search; see $OUT"
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
    local gtchk audit_out _t0
    _t0=$(date +%s)
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
    stage_line "per-game audit ($MODE)" $(( $(date +%s) - _t0 ))
  fi

  log ""
  log "--- stage wall times (this run; tier wall so far $(( $(date +%s) - RUN_T0 ))s) ---"
  local sl
  for sl in "${STAGE_LINES[@]}"; do echo "$sl" >> "$OUT"; done
  log ""
  log "Result: $PASS passed, $FAIL failed, $NEW new   (batch makespan ${TOTAL}s = $((TOTAL/60))m$((TOTAL%60))s)"
  if [ "$FAIL" -eq 0 ]; then
    [ "$NEW" -gt 0 ] && log "ALL PASS ($NEW new key(s); inspect, then 'bash test/regression.sh $MODEFLAG --accept' to record)" \
                     || log "ALL PASS"
    return 0
  else
    log "REGRESSION DETECTED"
    return 1
  fi
}

RC=0
for _mode in "${MODES[@]}"; do
  select_mode "$_mode"
  report_mode || RC=1
done
[ ${#MODES[@]} -gt 1 ] && echo "" && echo "Combined (${MODES[*]}): $([ $RC = 0 ] && echo ALL PASS || echo REGRESSION DETECTED)   (tier wall $(( $(date +%s) - RUN_T0 ))s)"
exit $RC
