#!/usr/bin/env bash
# Shared harness primitives for test/ and scripts/.
#
# WHY THIS EXISTS. test/regression.sh already does pooled-batch execution, per-mode seed
# disjointness, fingerprint parsing and per-game diffing correctly. Every one-off A/B script
# re-implemented a subset of it, and re-implementing is how an A/B ends up measuring the wrong
# thing -- this repo has already burned a session on phantom nondeterminism that turned out to be
# a repro-config mismatch, and two scripts here (fd_quick_ab.sh, fd_overnight_ab.sh) hard-coded
# `build/Release/mtg.exe` with no fallback, so they could not run on Linux at all.
#
# Source it from a script's directory-independent preamble:
#
#     set -uo pipefail
#     cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
#     . test/lib/harness.sh
#
# Then: BIN=$(harness_bin); h_manifest ... ; h_batch ... ; h_avg ... ; h_wins_diff ...
#
# HARD RULES this library encodes (see CLAUDE.md) -- they are not style preferences:
#   * ONE pooled work queue. h_batch runs exactly one `--batch` over one manifest. Do NOT loop
#     h_batch per seed, per deck or per arm: every such loop strands cores on its own tail,
#     and any serial step between invocations idles the whole machine. Put every game of every job
#     of every arm into ONE manifest (bake per-arm settings into each job) and call h_batch once.
#     The only legitimate reason for a second h_batch is a second ARM that needs a different
#     process-level env (an MTG_* flag), which the manifest cannot express.
#   * NEVER wrap a run in `timeout`. A truncated run reads as a result and corrupts an A/B.
#     Nothing here imposes one, and nothing calling it should.
#   * Logs go under logs/ or test/logs/, never the repo root.

# ---- guard against double-sourcing ---------------------------------------------------------
[ -n "${_MTG_HARNESS_LIB:-}" ] && return 0
_MTG_HARNESS_LIB=1

# ---- binary resolution ---------------------------------------------------------------------
# One place that knows where the engine lives. Honours $MTG_BIN (used by the regression harness
# to drive a SNAPSHOT copy of the binary, so a rebuild mid-run cannot swap it), then the
# multi-config layout: build/Release/mtg.exe on Windows/MSVC, build/Release/mtg elsewhere.
# Prints the path; returns 1 and explains itself if nothing is built.
harness_bin() {
    local b
    if [ -n "${MTG_BIN:-}" ]; then
        if [ -x "$MTG_BIN" ] || [ -f "$MTG_BIN" ]; then printf '%s\n' "$MTG_BIN"; return 0; fi
        echo "harness: MTG_BIN=$MTG_BIN does not exist" >&2; return 1
    fi
    for b in ./build/Release/mtg ./build/Release/mtg.exe; do
        [ -f "$b" ] && { printf '%s\n' "$b"; return 0; }
    done
    echo "harness: no engine binary at build/Release/mtg[.exe] -- run ./build.sh first" >&2
    echo "harness: (do NOT run cmake directly; a bare cmake leaves CMAKE_BUILD_TYPE empty = -O0)" >&2
    return 1
}

# Same, for the analyzer.
harness_analyze_bin() {
    local b
    for b in ./build/Release/mtg-analyze ./build/Release/mtg-analyze.exe; do
        [ -f "$b" ] && { printf '%s\n' "$b"; return 0; }
    done
    echo "harness: no analyzer at build/Release/mtg-analyze[.exe] -- run ./build.sh first" >&2
    return 1
}

# ---- deck path resolution ------------------------------------------------------------------
# Decks live in per-deck FOLDERS: decks/<name>/<name>.txt (or .cod) plus the generated profile
# and sibling models (docs/design/per-deck-folder-layout.md). Scripts that hard-coded the old flat
# `decks/<name>.txt` were silently orphaned by that move -- 14 of them still are. Resolve through
# these instead of writing the path, and a future layout change is one edit here.
#
# h_deck <name>  -> decks/<name>/<name>.{txt,cod}
h_deck() {
    local n=$1 p
    for p in "decks/$n/$n.txt" "decks/$n/$n.cod"; do
        [ -f "$p" ] && { printf '%s\n' "$p"; return 0; }
    done
    echo "harness: no decklist for '$n' (looked for decks/$n/$n.{txt,cod})" >&2
    echo "harness: decks present: $(ls -d decks/*/ 2>/dev/null | xargs -n1 basename | tr '\n' ' ')" >&2
    return 1
}

# h_profile <name>  -> decks/<name>/<name>.profile.json
h_profile() {
    # NOTE: separate statements. `local n=$1 p="...$n..."` expands $n BEFORE the assignment of n
    # takes effect (bash expands all words of the command first), which under `set -u` aborts with
    # "n: unbound variable" -- inside a command substitution that just yields an empty path.
    local n=$1
    local p="decks/$n/$n.profile.json"
    [ -f "$p" ] && { printf '%s\n' "$p"; return 0; }
    echo "harness: no profile for '$n' at $p -- run scripts/analyze_deck.py $(h_deck "$n" 2>/dev/null)" >&2
    return 1
}

# h_require <path>...  -- fail loudly and early if any input is missing.
# A missing deck or profile does not stop a long run: the engine may fall back to defaults and
# still print an avg, so the run LOOKS like a result. Assert inputs before spending hours.
h_require() {
    local p bad=0
    for p in "$@"; do [ -e "$p" ] || { echo "harness: missing required path: $p" >&2; bad=1; }; done
    [ "$bad" -eq 1 ] && { echo "harness: aborting before the run rather than producing a bogus result" >&2; return 1; }
    return 0
}

# ---- seed sets: ONE source of truth for train-vs-held-out ----------------------------------
# The suite's three modes use DISJOINT seeds so a heuristic tuned on the regression seeds can be
# validated on seeds it never saw. An ad-hoc script that tunes and validates on the same seed is
# measuring its own fit. Mirrors test/regression_cases.sh; keep them in step.
h_seeds_smoke()    { echo "1001"; }                # quick signal only, not a measurement set
h_seeds_train()    { echo "2002 3003"; }           # regression mode -- TUNE here
h_seeds_heldout()  { echo "4004 5005 6006 7007"; } # overnight mode -- VALIDATE here, never tune

# ---- manifest construction -----------------------------------------------------------------
# h_job <name> <deck-file> <profile> <games> <seed> [key=value ...]
#   Emits ONE manifest job object. Extra key=value pairs are passed through verbatim; a value
#   that is not a number and not true/false is quoted. Typical extras:
#     depth=3  budget_ms=100  weight=5000  ignore_play_profile=true  lookahead_bottoming=true
#   OMIT `depth` to let the deck's value_play block own the play depth (the d5 "value_play-driven"
#   case); PIN depth for a shallow coverage case and add ignore_play_profile=true, which is what
#   the enabled block's depth lock requires. Getting this pairing wrong is the classic repro
#   mismatch -- see docs/design/value-leaf-fallback-table.md.
h_job() {
    local name=$1 deck=$2 prof=$3 games=$4 seed=$5; shift 5
    local out kv k v
    out=$(printf '  { "name": "%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s' \
                 "$name" "$deck" "$prof" "$games" "$seed")
    for kv in "$@"; do
        k=${kv%%=*}; v=${kv#*=}
        case "$v" in
            ''|*[!0-9.-]*) [ "$v" = true ] || [ "$v" = false ] || v="\"$v\"" ;;
        esac
        out+=$(printf ', "%s": %s' "$k" "$v")
    done
    # NEWLINE-terminated: h_manifest consumes one job per line. Emitting these unterminated
    # silently DROPPED the last job (read returns non-zero on a final unterminated line) --
    # a manifest one job short still runs and still reports, so it reads as a result.
    printf '%s }\n' "$out"
}

# h_manifest <out-path> < (one h_job line per job on stdin)
#   Wraps job objects into a manifest. Reads job JSON from stdin, one per line, and commas them.
h_manifest() {
    local out=$1 line first=1 n=0
    mkdir -p "$(dirname "$out")"
    {
        echo '{ "jobs": ['
        # `|| [ -n "$line" ]` so a final line with no trailing newline is still consumed rather
        # than silently dropped -- a short manifest still runs and still prints an avg, so a
        # dropped job reads as a result instead of an error.
        while IFS= read -r line || [ -n "$line" ]; do
            [ -z "$line" ] && continue
            [ $first -eq 1 ] && first=0 || printf ',\n'
            printf '%s' "$line"
            n=$((n+1))
        done
        printf '\n] }\n'
    } > "$out"
    [ "$n" -eq 0 ] && { echo "harness: h_manifest got no jobs -> $out" >&2; return 1; }
    printf '%s\n' "$out"
}

# ---- pooled batch execution ----------------------------------------------------------------
# h_batch <bin> <manifest> <outdir> [label]
#   Runs ONE pooled batch. Per-job lines stream live (tee) AND land in <outdir>/<label>.log;
#   stderr in <outdir>/<label>.err; per-game win turns in <outdir>/<label>/ as <job>.wins.
#   Echoes the log path. Sets H_BATCH_SECONDS to the wall time.
#   --threads 0 = all cores; the runner's LPT scheduler handles the imbalance, which is exactly
#   the property a loop of small invocations throws away.
h_batch() {
    local bin=$1 manifest=$2 outdir=$3 label=${4:-batch} start rc
    mkdir -p "$outdir/$label"
    start=$(date +%s)
    "$bin" --batch "$manifest" --threads "${H_THREADS:-0}" --game-log-dir "$outdir/$label" \
        2>"$outdir/$label.err" | tee "$outdir/$label.log"
    rc=${PIPESTATUS[0]}
    H_BATCH_SECONDS=$(( $(date +%s) - start ))
    [ "$rc" -ne 0 ] && echo "harness: batch exited $rc (see $outdir/$label.err)" >&2
    printf '%s\n' "$outdir/$label.log"
    return "$rc"
}

# ---- result parsing ------------------------------------------------------------------------
# The runner prints one "<name>: played=P avg=A digest=D" line per job.
#
# THE METRIC is `avg` = mean turn-to-win with an UNWON game scored max_turns+1 (loss-penalized).
# Lower is better; a negative delta is an improvement. Goldfish win/loss counts are noise and are
# deliberately not reported -- do not reintroduce them as a headline number.

# h_avg <log> <job-name>  -> the avg, or empty if the job is absent
h_avg() {
    grep -m1 "^$2: " "$1" 2>/dev/null | sed -nE 's/.*[^a-z]avg=([0-9.]+).*/\1/p'
}

# h_digest <log> <job-name>  -> the play digest, or empty
h_digest() {
    grep -m1 "^$2: " "$1" 2>/dev/null | sed -nE 's/.*digest=([0-9a-f]+).*/\1/p'
}

# h_jobs <log>  -> every job name in the log, in completion order
h_jobs() { sed -nE 's/^([A-Za-z0-9_.-]+): played=.*/\1/p' "$1" 2>/dev/null; }

# h_delta <baseline-log> <variant-log> [label-a] [label-b]
#   Per-job avg table plus the mean delta. Negative total = the variant is BETTER.
#   Prints a table to stdout; sets H_DELTA_MEAN.
h_delta() {
    local base=$1 var=$2 la=${3:-base} lb=${4:-variant} job a b d sum=0 n=0
    printf '%-34s %10s %10s %10s\n' "job" "$la" "$lb" "delta"
    for job in $(h_jobs "$base"); do
        a=$(h_avg "$base" "$job"); b=$(h_avg "$var" "$job")
        if [ -z "$a" ] || [ -z "$b" ]; then
            printf '%-34s %10s %10s %10s\n' "$job" "${a:--}" "${b:--}" "MISSING"; continue
        fi
        d=$(awk -v x="$a" -v y="$b" 'BEGIN{printf "%+.4f", y-x}')
        printf '%-34s %10s %10s %10s\n' "$job" "$a" "$b" "$d"
        sum=$(awk -v s="$sum" -v x="$a" -v y="$b" 'BEGIN{printf "%.6f", s + (y-x)}'); n=$((n+1))
    done
    H_DELTA_MEAN=$(awk -v s="$sum" -v n="$n" 'BEGIN{printf "%+.4f", (n ? s/n : 0)}')
    echo ""
    echo "mean delta ($lb - $la) over $n jobs: $H_DELTA_MEAN   (negative = $lb better)"
}

# ---- per-game comparison -------------------------------------------------------------------
# The aggregate can be flat while individual games move in both directions. The per-game diff is
# what the accept flow requires you to inspect, and it is what names the game to reproduce.
#
# h_wins_diff <baseline-wins-dir> <variant-wins-dir> [job-filter]
#   Compares <dir>/<job>.wins pairs and prints per-job slower/faster/play-changed plus the changed
#   game indices. Sets H_SLOWER / H_FASTER / H_PLAYCHANGED.
#
#   The .wins format is "<game_index> <win_turn> <play_digest>" (the digest column is optional in
#   older logs). Two things matter and both are easy to get wrong:
#     * Index by GAME INDEX, never by line position. `paste` silently mis-pairs if the two runs
#       wrote their games in a different order, and reads the digest column as a win turn.
#     * Score LOSS-PENALIZED. An unwon game is recorded with win_turn <= 0 and must rank WORSE
#       than any win, not better -- a naive numeric compare makes every loss look like turn 0,
#       i.e. the fastest possible win. Same sentinel convention as test/audit_changed_games.py.
#   A game whose win turn is unchanged but whose digest moved is "play-changed": same score,
#   different line. Not a regression, but it means the change was not byte-identical.
h_wins_diff() {
    local bdir=$1 vdir=$2 filter=${3:-} f job vf sl fa pc changed ts=0 tf=0 tp=0
    printf '%-34s %8s %8s %13s\n' "job" "slower" "faster" "play-changed"
    for f in "$bdir"/*.wins; do
        [ -f "$f" ] || continue
        job=$(basename "$f" .wins)
        if [ -n "$filter" ]; then case "$job" in $filter) ;; *) continue ;; esac; fi
        vf="$vdir/$job.wins"
        [ -f "$vf" ] || { printf '%-34s %8s\n' "$job" "MISSING-VARIANT"; continue; }
        read -r sl fa pc changed < <(awk '
            function sc(t) { return (t > 0) ? t : 10000 }   # a loss ranks worse than any win
            function sh(t) { return (t > 0) ? t : "LOSS" }
            FNR==NR { ow[$1]=$2; od[$1]=$3; next }
            ($1 in ow) {
                if      (sc($2) > sc(ow[$1])) { slower++; ch=ch" "$1":"sh(ow[$1])"->"sh($2) }
                else if (sc($2) < sc(ow[$1])) { faster++; ch=ch" "$1":"sh(ow[$1])"->"sh($2) }
                else if (od[$1] != "" && $3 != "" && od[$1] != $3) { played++ }
            }
            END { print slower+0, faster+0, played+0, ch }' "$f" "$vf")
        printf '%-34s %8s %8s %13s\n' "$job" "$sl" "$fa" "$pc"
        if [ "$sl" -gt 0 ] || [ "$fa" -gt 0 ]; then echo "    changed (gi:base->variant):$changed"; fi
        ts=$((ts+sl)); tf=$((tf+fa)); tp=$((tp+pc))
    done
    H_SLOWER=$ts; H_FASTER=$tf; H_PLAYCHANGED=$tp
    echo ""
    echo "TOTAL slower=$ts faster=$tf play-changed=$tp   (slower = variant scores WORSE; a win"
    echo "becoming unwon is just the maximal slowdown, not a separate category)"
}

# ---- reproducing one game ------------------------------------------------------------------
# h_repro_cmd <bin> <deck> <profile> <depth> <base-seed> <game-index>
#   Prints the command that reproduces a SINGLE game out of a batch job. Encodes the repro contract
#   the repo has already burned a session getting wrong (it read as engine nondeterminism):
#     * the per-game seed is BASE_SEED + gi, not the base seed (GoldFishRunner.cpp: SetupGame is
#       called with base_seed + gi), AND
#     * --game-index gi is still required, because it selects the opponent SPAWN PATTERN
#       ((base_game_index + i) % 10) independently of the shuffle seed. Passing one without the
#       other reproduces a different game and looks like a flapping engine.
#     * at the searched depth on a value_play deck, OMIT --depth so the deck's value_play block
#       owns it; to PIN a shallow depth instead you must also pass --ignore-play-profile, which is
#       what bypasses that block's depth lock (pass depth= to get this form).
#   Pass depth as "value_play" or empty for the block-driven form.
h_repro_cmd() {
    local bin=$1 deck=$2 prof=$3 depth=$4 seed=$5 gi=$6 gseed
    gseed=$(( seed + gi ))
    if [ "$depth" = "value_play" ] || [ -z "$depth" ]; then
        printf '%s %s --profile %s --games 1 --seed %s --game-index %s --log-dir logs/repro\n' \
               "$bin" "$deck" "$prof" "$gseed" "$gi"
    else
        printf '%s %s --profile %s --games 1 --seed %s --game-index %s --depth %s --ignore-play-profile --log-dir logs/repro\n' \
               "$bin" "$deck" "$prof" "$gseed" "$gi" "$depth"
    fi
}
