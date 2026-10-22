#!/usr/bin/env bash
# THE value-leaf pipeline. One script, one route, for ONE deck or the whole fleet.
#
#   bash scripts/valueleaf.sh run    decks/<Deck>     # build a model (new deck or regeneration)
#   bash scripts/valueleaf.sh status decks/<Deck>     # progress, touches nothing
#   bash scripts/valueleaf.sh run                     # the whole fleet
#
# THE INTERFACE IS THE DECK. There are no knobs -- every setting that matters is FIXED here rather
# than left to a caller to remember, and a retired one is REJECTED rather than ignored. Each has
# already gone wrong once:
#
#   * INCREMENTAL BATCHING, ALWAYS. Every phase is one pooled queue, resumable, with concurrent
#     batches per cell. The monolithic path is removed. A per-item loop pays a load-imbalance tail
#     PER ITEM, and one-batch-per-cell scheduling starved a live run to 3 of 24 cores for hours.
#   * NO CONDEMNATION AT d<=5. The H cells ARE the crossover -- they decide which evaluator
#     escalation uses at each rung it climbs to -- so condemning one leaves a HOLE in the answer
#     rather than saving cost. The guard is wall-clock based, so which cells it hits is partly luck
#     (H3 measured 219.8 s/game on one seed and ~30 on two others). Clamped, not just defaulted.
#   * THE PROFILE IS ALWAYS ATTACHED. Measuring profile-less describes a deck we do not ship; it
#     invalidated every value-leaf table in this repo once already.
#   * THE STAGED MODEL EXISTS BEFORE THE MATRIX. The H-cell ladder is guarded on the sidecar
#     existing, so a missing one does not error -- it silently runs every H cell on the slow path.
#   * SLOW GAMES AND UTILISATION ARE REPORTED, by the engine itself. `mtg --batch` defaults both ON:
#     any game over 30 s prints a self-contained repro (tagged by cell, mirrored to
#     <queue>/slow_games.log) and a `[batch] heartbeat` line every 10 min leads with workers busy.
#     They are not passed from here any more -- doing so is how they came to be OFF for every other
#     caller, which is why a 23-hour run at 3 of 20 cores produced no signal.
#
# A BETTER LEAF IS ADOPTED AUTOMATICALLY (USER 2026-10-11: "We should adopt automatically if the leaf
# is better. Nothing prevents you from updating or asking the user as long as you do not stop.").
# "Better" is primarily PERFORMANCE: cheaper, with quality not notably worse (vlq_ab_report.py
# --verdict). Models are staged in logs/eval/<stem>.value.STAGED.json; phase E.5 installs one as the
# live <stem>.value.json IN THIS CHECKOUT when phase E's A/B says so, and the run goes on to F. A leaf
# that is not adopted (dearer, or notably worse) is left staged for the user and the run CONTINUES. Nothing is committed. Every decision taken without stopping -- adoptions, and any
# question a gate would once have stopped on -- is appended to <queue>/DECISIONS.txt, which `status`
# and the COMPLETE line print until a human deletes it.
#
# New deck and regeneration are the SAME command -- the script detects whether a live sidecar exists
# and adapts (merge into a copy vs. use the fresh model directly; A/B against "no sidecar" vs. the
# old one). Resume is safe and incremental: finished phases are skipped via marker files, and rows
# dedupe on (seed,turn). See .claude/skills/value-leaf.md.
#
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

# ---------------------------------------------------------------- THE INTERFACE IS THE DECK ONLY
#
# `bash scripts/valueleaf.sh run decks/<Deck>` and nothing else. Every value below is FIXED, not a
# default, because each knob that existed here was a way to get a wrong answer that still looked
# like an answer: HDEPTHS silently truncated the ladder the crossover clamps to, MATRIX_TARGET
# resized the measurement, SLOW_GAME_MS could switch off the only instrument that names a
# pathological game, VLQ could point a single-deck run at the shared fleet directory (which is
# exactly how one run's slow games ended up in another's log).
#
# The ONE setting is NEVER_CONDEMN, kept deliberately (user, 2026-08-10): the d<=5 floor is the
# default we want, and raising it is a legitimate choice when a deeper ladder is being measured.
#
# A retired knob is REJECTED, not ignored -- silently dropping `HDEPTHS=1 2 3` would hand back a
# table that looks complete and is not. See RETIRED_KNOBS below.
RETIRED_KNOBS="VLQ WORKERS MATRIX_TARGET MATRIX_REF SLOW_GAME_MS SLOW_GAME_LOG VDEPTHS HDEPTHS
               INTRACTABLE_SPG AB_GAMES AB_SEEDS PLAY_GAMES PLAY_SEEDS ROW_K ROW_GAMES DECK_DIR"
_set_knobs=""
for _k in $RETIRED_KNOBS; do
    if [ -n "${!_k+x}" ]; then _set_knobs="$_set_knobs $_k"; fi
done
if [ -n "$_set_knobs" ]; then
    echo "REFUSING TO RUN: these settings were retired and are no longer honoured:$_set_knobs" >&2
    echo "  The interface is the deck: $0 run decks/<Deck>" >&2
    echo "  Every phase setting is fixed in the script on purpose -- a knob here produces a table" >&2
    echo "  that looks complete but measures something else. NEVER_CONDEMN (floor 5) is the one" >&2
    echo "  setting that remains. See .claude/skills/value-leaf.md." >&2
    exit 2
fi

VLQ=logs/vlq              # fleet default; single-deck mode scopes this to logs/vlq_<key> below
DONE=$VLQ/done
ROWDIR=$VLQ/rows          # queue-owned: logs/eval collides case-insensitively on this filesystem
                          # (Auras_value.rows resolves to the pre-merge auras_value.rows)
mkdir -p "$DONE" "$ROWDIR" logs/eval

# NO worker count is passed. The engine resolves it to AffinityCpuCount() -- every core the process
# is actually allowed to use -- which beats any number chosen here: it is affinity-aware (nproc and
# os.cpu_count() both over-report inside a cgroup with a restricted cpuset), and it cannot drift out
# of date with the box.
#
# The old value was 20 with the note "~1 GB/process on a 47 GB box". That reasoning expired when the
# matrix stopped spawning one PROCESS per cell: it is now ONE process whose workers SHARE each
# mulligan profile through a shared_ptr, and measured peak RSS at 24 threads on the heaviest keep
# model in the repo (Goblins) is 1,481 MB -- 3% of the box. There was never a reason to leave four
# cores idle on a run whose entire failure mode is idle cores. (User, 2026-08-10: "Why do we want
# less than the maximum workers? This makes no sense to me." It did not.)
WORKERS=auto
MATRIX_TARGET=400
MATRIX_REF=50                     # games cap for cells ruled intractable
# Protect the d<=5 ladder from the tractability guard. The H cells ARE the crossover: they decide
# which evaluator escalation uses at each rung it climbs to, so condemning one leaves a HOLE in the
# answer rather than saving cost. FiveColour 2026-08-08 is the worked example -- 8 capped cells, all
# of them H (H4 on every seed, H5 on three, one with zero games), while all 20 V cells finished at
# 400. That makes crossover entries for c=4..8 unfounded for a deck that ships at d5. The guard is
# also wall-clock based, so it can fire purely because the box was busy.
# THE ONE SETTING (user, 2026-08-10). Default 5 -- never condemn at or below the shipped play
# depth -- and raisable when a deeper ladder is being measured. It cannot go BELOW 5 (clamped
# just below), because that is the case that silently holes the answer rather than saving cost.
NEVER_CONDEMN=${NEVER_CONDEMN:-5}
# Clamped, not merely defaulted. The d<=5 H cells ARE the crossover -- they decide which evaluator
# escalation uses at each rung it climbs to -- so condemning one leaves a HOLE in the answer rather
# than saving cost, and the guard is wall-clock based so which cells it hits is partly luck. There is
# exactly ONE route here: incremental batching, no condemnation at d<=5.
if [ "$NEVER_CONDEMN" -lt 5 ] 2>/dev/null; then
    echo "NEVER_CONDEMN=$NEVER_CONDEMN is below the floor; forcing 5 (see docs/design/value-leaf-regeneration-queue.md)"
    NEVER_CONDEMN=5
fi
# Slow-game reporting is no longer set here: `mtg --batch` defaults it ON at 30 s and also prints a
# 10-minute `[batch] heartbeat` line leading with worker utilisation. Passing it from this script was
# how it came to be OFF for every caller that was not this script.
VDEPTHS="1 2 3 4 5 6 7 8"
# The HEURISTIC ladder, 1-5 -- H5 is the ESCALATION CAP, so it is also the deepest rung worth
# measuring (user, 2026-08-14). Escalation can never exceed H5, so H5 is the strongest fallback the
# runtime can take, so "trust the leaf" means "the leaf matches H5" and nothing deeper can inform a
# decision the runtime is incapable of making. The crossover's maxH+1 sentinel then reads as NEVER
# fall back, which is exactly the wanted behaviour.
#
# H6 WAS in this ladder and is removed. It has NEVER been completed on any deck in any run: every H6
# on disk is a small side-sample (fivecolour 4g, antilife 50g, dragonstorm 100g, all condemned; the
# 5-deck tables' H6 was merged in from a 150-GAME `d68` pass into tables declaring games=500 --
# `_antilife_d68_avg.txt` and `_5deck_combined.txt` both carry H6=4.6933, byte-identical). The
# numbers say the same: H4->H5 across all decks is 0.0000-0.0033 while H5->H6 ranges -0.6048 to
# +0.1525, i.e. noise. The old rationale here -- "H6 came in ~11x CHEAPER than H5" -- is contradicted
# by the 2026-08 FiveColour run: H5=822515.7ms vs H6=991283.1ms per game, 1.2x MORE expensive, and
# that measured only on the 4-50 games that tripped the one-hour guard, so it is a floor. Keeping it
# cost 47.4 core-h of that run for 111 unusable games, supplied the H6 row that flipped the derived
# fallback rule from "never" to "fall back at H6", and its ragged condemned chunks (n=4, n=7) held two
# seeds' banking hostage. See docs/design/depth-matrix-degenerate-games.md.
#
# Do NOT cut to H4: the two decks still improving at H3->H4 (dragonstorm +0.1100, hinata +0.0850) are
# exactly the two whose H5 was never measured -- dragonstorm's was ATTEMPTED AND CONDEMNED. Depth
# stays useful roughly as far as a deck's games run (antilife wins turn 4.09, dies at H2->H3;
# fivecolour 4.98, dies at H4->H5; hinata's games run to turn 6.00). H5 is the conservative ceiling.
#
# Note H_maxturns == V_maxturns is the SAME cell: at depth >= max_turns the horizon covers the whole
# game, so every leaf is terminal and the learned evaluator is never consulted. FiveColour has
# max_turns=8, so H8 == V8 -- never pay ~68 core-hours to re-measure a cell the V arm already has.
HDEPTHS="1 2 3 4 5"
# Tractability guard: condemn a row whose MEDIAN game costs more than this many seconds.
#
# THE QUESTION IT ASKS is "can we escalate to this depth SOME of the time, at a cost that is not
# insane?" -- because that is the only regime the table's answer is ever applied in. Shipped play is
# BUDGETED, so it never processes a pathological position at depth; the deep unbounded games are not
# cases production will ever reach. So a row is usable when a TYPICAL game is affordable however ugly
# its tail (ABANDON_K truncates the tail), and unusable only when the MAJORITY of its games are
# impractical -- which is exactly "the median exceeds the limit". Nothing else can be salvaged there:
# skipping past that rate is reshaping the population, not trimming it (see --max-skip-frac).
#
# THIS WAS A MEAN AT 60 AND THE STATISTIC WAS WRONG, twice over. A mean conflates "many moderately
# slow games" (condemn) with "a few catastrophic ones" (abandon those, keep the row), which want
# opposite responses: one 32-minute game put V6/V7/V8@8008 1-3% over the old 60 s limit and condemned
# three healthy cells whose other 49 games ran ~19.5 s/game. And once ABANDON_K is armed the mean is
# not even well defined -- a ceiling'd game contributes its TRUNCATED cost, so a row with 10%
# abandoned games reads 3.4x its median at k=25 and 1.9x at k=10, i.e. the verdict follows an
# arbitrary constant. A median moves not at all.
#
# 30 s/game: healthy V6-V8 cells measured ~19.5 s/game typical, so this leaves ~1.5x headroom over
# the case we know we want to keep, and a row at the limit costs 400 games x 4 seeds x 30 s = 13.3
# core-hours. It is deliberately LOWER than the old 60 -- a mean carried tail inflation a median does
# not, so carrying the number across would have quietly loosened the guard by roughly 3x.
#
# NOTE only V6/V7/V8 are ever subject to this: NEVER_CONDEMN is clamped to >=5 and HDEPTHS tops out
# at 5, so every H cell is structurally exempt whatever the statistic says.
INTRACTABLE_MEDIAN_SPG=30
# PER-GAME WORK CEILING, relative. A game past ABANDON_K x the median cost of its own cell's first
# ABANDON_CALIB games is VOIDED -- no result, excluded from every cell of its (deck,seed), and
# backfilled so the cell still reaches its game count. This is the guard nothing else provides:
# INTRACTABLE_MEDIAN_SPG and the in-flight max_game_sec both condemn a ROW or CELL, and neither stops a game that
# is already running. That is the 2026-08 stall exactly -- a matrix finished 88% of 22,400 games and
# then sat for 26.5 more hours on six of them, one cell having been condemned at the one-hour mark
# while its game ran on for another 25. Roughly 1-2% of games carry about half of an arm's cost.
#
# RELATIVE, because absolute cannot work here: on burn -- the CHEAPEST deck in the repo -- six cells
# span 150x in median units (V1 50, H3 7,506), and this deck's H5 is orders further out again. One
# absolute number is either inert at the top of the ladder or shreds the bottom of it. The sample is
# a FIXED set of games (the cell's lowest offsets), never a running median, so the abandoned set
# stays reproducible across machines and resumes rather than following thread interleave.
#
# k=25 sits above the natural spread and far below the pathology: measured max/median per cell is
# 2.1-7.1x on burn and 11.2x on a FiveColour keep-gen sample, while the games this exists to stop are
# 100-1000x.
#
# calib=25 (user, 2026-08-16: "we should try at least 25 games and take the median there", under the
# standing constraint "we don't want a worse result than the original strategy"). It was 10 on the
# argument that every calibration game runs UNBOUNDED, so a bigger sample buys precision at the price
# of exposure. THAT ARGUMENT NO LONGER HOLDS: since the provisional prefix ceiling (BatchRunner's
# kCalibPrefix), only the cell's first THREE games are ever unbounded -- games 3..calib-1 run under
# 4 x k x the prefix median. The exposure is therefore a constant, independent of calib, and raising
# the sample is a one-way trade: a median of 25 is far less likely to land freakishly low than a
# median of 10, and a low median is precisely what condemns ordinary games. What calib does still buy
# is a longer HOLD (a cell's remaining games wait for its sample) -- a scheduling cost, per-cell and
# not a barrier, which 52 cells over 32 cores absorb.
ABANDON_K=25
ABANDON_CALIB=25
# ABSOLUTE FLOOR under the ratio (user, 2026-08-15: "it must be above 10 minutes... maybe even 30
# minutes. Beyond that, I think it's okay to condemn").
#
# WHY A RATIO ALONE IS NOT ENOUGH. `k x median` has no notion of how expensive a cell is in absolute
# terms, so on a CHEAP cell it condemns games that cost nothing. Measured on the Mirrorwing matrix
# before this existed: V1 (9.6 ms/game median) lost 6.3% of its games and H1 (611 ms) 6.0% -- pure
# data loss, since there is no cost explosion to protect against at that scale. And because a game
# abandoned in ANY cell is excluded from ALL of them (the driver unions the abandoned sets so every
# cell holds the same games), a cheap cell's spurious abandonment takes that game away from the
# EXPENSIVE rows too. The floor makes the ratio inert where it is not needed and leaves it governing
# the top of the ladder, which is the only place it was ever meant to act.
#
# CALIBRATION. The ceiling must stay in work UNITS -- that is what makes the abandoned set identical
# on every machine and the skip list shareable (ai/GameWorkMeter.h) -- but "30 minutes" is wall clock,
# and the conversion is WORKLOAD-dependent, not just deck-dependent.
#
# Calibrated from the Mirrorwing matrix ITSELF (median units per cell, from the CEILING lines, over
# that cell's reported ms/game):
#     H1 22.7k   H2 18.6k   H3 12.4k   H4 10.2k   H5 6.6k   units/core-second
#     V1 19.9k   V2 26.6k   V4 11.9k   V8 4.8k
# i.e. ~5k-27k, call it ~10k on the expensive cells where the monsters actually live. 1800 s x 10k
# ~= 18M, rounded to 20M.
#
# A FIRST CUT USED 250M AND WAS WRONG BY ~10x. It came from timing the comp scorer at depth 3 /
# budget 3 ms, which measured ~135k units/core-s -- a budget-limited shallow search, where nodes are
# cheap and plentiful. The matrix runs UNBOUNDED search against a value leaf, where each node costs
# far more wall-clock, so the same unit count buys an order of magnitude more time. At 250M the floor
# was ~7 HOURS: it would have let through the 6.41-hour game the phase-A heartbeat had already
# recorded, i.e. silently disabled the very mechanism it was meant to bound. CALIBRATE AGAINST THE
# WORKLOAD YOU ARE BOUNDING, not a convenient proxy.
#
# CONSEQUENCE, and it is intended: 25 x median tops out around 7.8M on this deck (H4), which is under
# the floor, so the ratio never fires here and the rule reduces to "abandon a game costing more than
# ~30 minutes". That is exactly the user's call ("it must be above 10 minutes... maybe even 30
# minutes. Beyond that, I think it's okay to condemn"). The ratio remains in force for any deck whose
# cells are expensive enough that 25 x median exceeds the floor.
ABANDON_FLOOR_UNITS=40000000
# PER-GAME WALL-CLOCK BACKSTOP, two stages (user, 2026-09-20: "I'm thinking 1.5-2 hours check (and
# stop if we have a lot of work left) and 3-4 hours hard cap"). Full design and the measurements that
# forced it: docs/design/per-game-wall-clock-backstop.md; mechanism: src/ai/GameWorkMeter.h.
#
# WHY EVERYTHING ABOVE IS NOT ENOUGH, which is the part that took a run to learn. Every guard in this
# file is denominated in work UNITS, and on precisely the games they exist to stop, units and wall
# clock come apart by an order of magnitude. Measured on Fungus phase C: four games reached the
# 40,000,000-unit ceiling -- dead on it, correct in its own currency -- after 2.5 to 4.3 HOURS,
# running at 830-2,600 units/s against a 36,439 units/s median over that run's own slow games. They
# were then abandoned and their work discarded, so the 4.26 h game produced nothing at all. Across
# that census 61% of all slow-game wall time (95.8 of 156.6 core-hours) produced nothing.
#
# And a cell whose ceiling is DISARMED has no bound in EITHER currency: past --max-skip-frac the
# driver's skip_capped zeroes abandon_units, abandon_k AND abandon_floor_units together, which is
# exactly the state Snow's matrix reaches at ~offset 98 (docs/design/depth-matrix-degenerate-games.md).
# The backstop is the only thing that reaches those games.
#
# THE TWO STAGES DO DIFFERENT JOBS:
#   STAGE 1 (predictive, 2 h) extrapolates from the game's own unit rate: if it cannot reach its unit
#     ceiling before the hard cap, cut it now instead of spending 1.5 more hours proving it. It is
#     REPRODUCIBILITY-NEUTRAL given stage 2 -- it only ever cuts games stage 2 would have killed
#     anyway, for the same verdict and the same skip list -- so it is free.
#   STAGE 2 (hard, 3.5 h) is unconditional and is the one that bounds the run.
#
# NOT A COST CONTROL, and the distinction is load-bearing. The unit ceiling stays the real bound: it
# is deterministic, it is a function of the data, and it is what decides which games are kept. This
# only catches the case where that bound has stopped working. Sized to be a NON-EVENT -- it should
# fire on the 4 h class and never on the 1-2 h class. If it fires with any regularity, ABANDON_K is
# mis-set for that cell and that is the bug to fix instead of tightening this.
MAX_GAME_PREDICT_SEC=7200    # 2 h   -- stage 1, within the user's 1.5-2 h
MAX_GAME_WALL_SEC=12600      # 3.5 h -- stage 2, within the user's 3-4 h
# The SAME number is also passed as --abandon-units, i.e. as the ABSOLUTE cap that applies DURING the
# calibration window. Without it a cell whose first games are all monsters never completes a
# calibration sample, so no median exists, so no relative ceiling ever arms, so those games run
# unbounded until the wall-clock rule condemns the WHOLE CELL. That is not hypothetical: it is exactly
# how V6/V7/V8 on seed 11011 were lost (condemned at 3656-4188 s with games=0 and ceil=0), while H5 on
# the same seed survived a LONGER game (4200 s) purely because its ceiling had armed. The floor only
# reaches the relative path, which needs a median that never arrived.
# MEMORY BOUNDS, derived from the box -- generous, but never OOM (user, 2026-08-13). Both caches are
# RESULT-NEUTRAL memos (a refused insert just recomputes; play is byte-identical -- see
# TranspositionTable::Cap and FslCap in TurnSolver.cpp), so capping them costs only tail-game
# wall-clock, never a row or a decision. They MUST be capped here because the label path runs
# UNBOUNDED search on every worker at once and its two caches are otherwise unlimited:
#   * MTG_TT_CAP  -- transposition table, ~64 B/entry, grows for the whole game.
#   * MTG_FSL_CAP -- per-decision line cache, HEAVY entries (a full SearchLine of per-phase plans);
#     ONE Mirrorwing mass-draw decision was measured at ~28 GB uncapped (2026-08-11), and phase A
#     with both caches unlimited on 32 workers OOM'd a 23 GB box 3.2 h in (2026-08-13).
# Sizing: budget = MemTotal minus 5 GB (system + engine baseline), split per worker; TT takes 1/3 at
# 64 B/entry, the line cache 2/5 in REAL KB (byte-accurate pool, see its own block below).
# History of the old entry-count planning size: the first cut assumed 8 KB/entry out of
# pessimism and that guess was the dominant cost of the whole phase: a three-cap probe on a heavy
# label game (900011, 2026-08-13) measured ~600 B/entry from the RSS/entries slope and a 3-5x WALL
# penalty from cap recompute (uncapped 58 s vs 295 s at cap 10K, peak RSS 125 MB), while the phase's
# 6-hour monster games sat at 0.2 GB RSS -- strangled by entry count with 14 GB of budget unused.
# 1 KB keeps ~40% headroom over the measurement; the box only OOMs if real entries exceed it by 3x
# CONCURRENTLY on every worker, which the measured sizes put far out of reach. On a 47 GB /
# 24-worker box this lands near the matrix driver's long-standing MTG_TT_CAP=8000000.
# Exported for every phase; the matrix driver's env.setdefault yields to these.
# THE DERIVATION LIVES IN scripts/lib/membudget.sh (shared with mullgen.sh since 2026-09-06 --
# mullgen used to export NO caps, i.e. every long mulligan generation ran unbounded). The comment
# blocks below record the measured history behind each number; the executable lines are the lib's.
. "$(dirname "${BASH_SOURCE[0]}")/lib/membudget.sh"
# COUNT EVERY CONSUMER (2026-09-05, third OOM of the day): TT+FSL used to take the WHOLE budget,
# but a 32-thread phase A also holds ~200 MB/thread of consumers the formula never counted --
# enummemo (8192 entries/thread of whole vector<Plan> results, measured ~3 GB total on Melira),
# solvememo (16384 entries/thread, ~1.5 GB), and transient per-game search state (all-strangled
# floor measured ~2.5 GB and flat). Their sum put the honest TT+FSL bounds over the box anyway
# (2.5+3+1.5+6.4+7.7 ~ 21 GB on 23 GB). Reserve them per-thread first; TT+FSL split the REMAINDER.
# The plan caches (enummemo promotions + the bp-enum continuation cache) get an explicit
# per-THREAD byte budget INSIDE the reserve (MTG_PLAN_CACHE_KB, engine-side, 2026-09-06): their
# 8192-ENTRY caps bound nothing in bytes when one entry is a whole combo-turn vector<Plan> --
# that unbounded promotion is what spiked phase A 5 GB -> 23 GB in ~2 min and OOM'd the box five
# times on 2026-09-05/06. 100 MB/thread keeps the memos' measured wall win (they were adopted
# for perf) with an honest bound; a refused store just recomputes (result-neutral).
# The line-cache budget is a SHARED POOL, not a per-worker slice (MTG_FSL_POOL, engine-side global):
# appetite is heavily skewed (typical game ~100 MB, monster ~900 MB, measured 2026-08-14), so a
# uniform slice strangled the monsters 3.3x while most of the budget idled. MTG_FSL_CAP stays as a
# generous PER-DECISION bound so one pathological decision (the ~28 GB analyzer case) cannot drain
# the pool for everyone else.
# THE POOL IS IN REAL KB since 2026-09-05 (byte-accurate accounting, ApproxFslKb in TurnSolver.cpp).
# It used to be an ENTRY count converted from bytes at a planning size calibrated on one deck
# (~600 B measured, 1 KB planned) -- but entry size is DECK-DEPENDENT, and Melira's combo-turn
# SearchLines at ~3-4 KB/entry turned the nominal 12.3 GB pool into ~20 GB real: phase A OOM'd this
# 23 GB box twice (2026-09-05, kernel kills at ~27 GB anon with swap exhausted; a solo monster game
# peaks ~200 MB, so it was pure 32-worker aggregation). Byte accounting makes the number below a
# bound that holds for ANY deck, so no planning-size guess remains to go stale.
# Share: 2/5 of the post-reserve cache budget (enummemo/solvememo/floor are reserved above),
# with slack for the KB estimate erring low. On the 23 GB / 32-thread box this lands near
# TT 4.1 GB + FSL 5.0 GB + reserve 6.4 GB ~ 15.5 GB engine worst case.
AB_GAMES=1000
AB_SEEDS="600000 601000 602000 603000 604000 605000 606000 607000"
# The leaf-rule A/B (leafrule_counterfactual) adds ONE arm, leafOFF, on these same seeds: it pairs against
# the staged arm, which already is the ON product.
# (The play-profile target_depth sweep's PLAY_GAMES / PLAY_SEEDS are gone with the sweep: phase E.7,
# scripts/play_setting_stage.py, measures and DECIDES the play setting on the leaf that ships.)
# TRUST ACCEPTANCE arms (user, 2026-08-15): the depth matrix nominates a `value_trust_depth`
# CANDIDATE and these games decide it, because trust is a claim about keeping leaf lines inside real
# BUDGETED play and the matrix measures the two arms separately and unbounded. Same power as the
# adoption A/B (8 seeds x 1000) because it IS an adoption decision.
#
# SEEDS DISJOINT FROM EVERYTHING, and spaced exactly games-apart so the arms tile their seed space
# once: game identity is base+game_index, so closer spacing makes jobs REPLAY games -- which once
# turned 1.3 sigma into a fake -14.4 sigma. 620000+ sits clear of AB (600000-607999) and play
# (610000-613999). vlq_trust_accept.py re-checks the tiling rather than trusting this comment.
TRUST_GAMES=1000
TRUST_SEEDS="620000 621000 622000 623000 624000 625000 626000 627000"
# Non-inferiority margin for the acceptance test, in LP turns. Same number as the deriver's --tol,
# and deliberately so: the tolerance's ONLY job is now to gate this test.
TRUST_TOL=0.002
# PHASE E0, the EMULATED LADDER's per-deck R calibration (USER 2026-10-21): play-settings games of the staged model
# in the emulated ladder with per-game R calibration, read back per deck (MTG_ROLLOUT_STATS) and frozen into the
# phase-E arm's emul_r. Seeds clear of AB (600000-607999), play (610000-613999) and trust (620000-627999). 400
# games put 100s-1000s of samples on depths 1-4 of every deck in the 2026-10-21 fleet calibration.
EMULCAL_GAMES=200
EMULCAL_SEEDS="630000 631000"
ROW_K=3
# PHASE A PER-GAME WALL BACKSTOP (user, 2026-10-09: "just to be safe we should do a similar
# condemnation approach to phase C in A" / "It doesn't win us much to grind a couple of games for that
# many hours" / "just a wall-cap is okay. However, we need to let it be relatively soft. Maybe 3-4
# hours?"). Phase A was the ONLY phase with no tail bound, and on kitty v3 that cost a whole box: two
# games held 30 of 32 cores for SIX HOURS after the last row was written, one reaching 9.9 h.
#
# 4 h, the SOFT end of the range asked for, chosen on a property of the measured distribution rather
# than taste: over that run's 346 slow games the worst that actually COMPLETED ran 14,387 s = 3.996 h,
# so a 4 h cap would have cut NOTHING that finished -- only the two games that never finished at all.
# It still recovers the wall, which is the point rather than the core-hours: the last row was written
# at 15:21 and the pinned games started ~11:22, so a 4 h cap ends phase A around 15:25 instead of
# 21:16 -- ~5.8 h, nearly all of it with 30 of 32 cores idle. The cap curve, for re-deciding later:
#     cap    games cut   core-h recovered        (2500 games queued)
#     30 min    19           37.0
#      1 h      10           29.2
#      2 h       3           20.1
#      4 h       0          ~12                  <- ADOPTED
# NO PREDICTIVE STAGE: the user asked for "just a wall-cap", and stage 1 would make it less soft.
#
# THIS ONLY WORKS BECAUSE OF THE WALK LIVENESS POLL (TurnSolver.cpp, kWalkAbandonStride, 2026-10-10).
# A wall deadline is observed only where something polls gamework::Abandoned(); the greedy subset walk
# polled nothing, which is how phase C ran a game 4.07 h past a 3.5 h cap. Without that poll this cap
# would silently fail on exactly the games it is here for. Do not raise/trust it independently of it.
#
# SOFTER THAN PHASE C ON PURPOSE. There a wall-cut game is excluded from every cell alike and phase
# C.5 must PROVE it changed no verdict; here the only consequence is fewer training rows, and rows are
# ADDITIVE -- a later run at a higher cap adds them back, and `finish` already establishes that fewer
# rows is acceptable. A cut game keeps every row it already banked; only its LATE turns are lost.
# HONEST CAVEAT: a wall cut is not deterministic, so which rows exist becomes mildly machine-dependent
# for the few games that reach the cap. The unit ceiling WOULD be deterministic but is measured
# incapable of reaching this pathology (the worst completed game sustained 186 units/s against a
# healthy ~4,800), so there is no deterministic option on offer.
# USER-OWNED NUMBERS. 0 forces a stage off (EnvInt treats -1 as unset, 0 as off).
ROWS_PREDICT_SEC=${ROWS_PREDICT_SEC:-0}
ROWS_WALL_SEC=${ROWS_WALL_SEC:-14400}
# Fail out when a LOT of games are super slow (user: "if there are a lot of games condemned we should
# probably fail out" / "if Phase A has a lot of super slow games it is a real signal that we need
# optimizations"). % of queued games running past HALF the cap. 0 disables. Baseline for scale, kitty
# v3: 3 games >= 2 h (0.12%), 10 >= 1 h (0.40%), 19 >= 30 min (0.76%) -- so 2% is ~17x the worst deck
# measured. Same response phase C.5 prescribes: a pile of condemnations is an ENGINE-OPTIMISATION
# signal "that a bigger ceiling would only hide", never a cap to loosen.
ROWS_SLOW_FAIL_PCT=${ROWS_SLOW_FAIL_PCT:-2}
# PER-POSITION LABEL CEILING for phase A -- NOT SET. Left at the engine default deliberately.
#
# USER, 2026-09-20: *"I don't want you fiddling with the general settings here. Optimizations are
# good, but this is not an optimization."* Correct, and the distinction is the point: lowering this
# does not make the same work cheaper, it CHANGES WHICH POSITIONS GET LABELLED -- i.e. it changes the
# training set, which is a methodology decision and the deck owner's call, not an agent's. An
# optimization may be adopted on evidence; an estimand change may not. The measurement below is kept
# as a PROPOSAL for that decision, not as a setting.
#
# The measurement, so the decision has numbers under it. A label is only written when the
# earliest-win search COMPLETES; a
# truncated search returns max_turns+1, which is byte-identical to a real refutation, so EmitEvalRows
# drops the position rather than teach the model that a position we could not AFFORD to solve is one
# we cannot WIN from. That is right -- but it means the per-position budget is the only thing bounding
# a position's cost, and the engine default (1e6 virtual ms = 900M units) is not a bound at all: on
# Snow, measured at ~128k units/s, it is roughly TWO HOURS per position.
#
# It went unnoticed because a second mechanism was accidentally capping the same cost. A breakpoint
# condemnation drop used to bump the truncation watermark, and EmitEvalRows `return`s (not
# `continue`s) on a truncated report -- so any position where the filter fired was abandoned at its
# first K-sample. Cheap, and wrong: the prune is lossless, so those were COMPLETE answers being thrown
# away (53.9% of Snow's positions). cd00879e stopped the bogus drop; this line supplies the real bound
# it was standing in for.
#
# WHAT A TIGHTER CEILING WOULD BUY, measured on the eight worst games in Snow's phase-A population
# (found from the batch heartbeat's in-flight list, and including seed 900807 gi57 -- the ledger's old
# 44.3 h monster). PROPOSAL ONLY -- nothing here is in force:
#
#   ceiling          positions kept        hardest games              worst game
#   1e6 (default)    100%                  >1.5 h each, NOT FINISHING unbounded in practice
#   30,000           90.7% (5 of 54 cut)   310-1166 s, mean 703 s     19 min
#   10,000           -                     207-296 s (2.7x cheaper)   -
#
# Those eight are the WORST games in the deck, so 90.7% is a floor on coverage, not an average -- an
# ordinary game never approaches the ceiling. Against the 46% that survived before, this keeps more
# labels AND bounds the run. Every drop it does cause is now an honest one, reported as
# `budget-ceiling=N` (see the LabelTruncationReport split, same commit).
#
# ROWS ARE BUDGET-INDEPENDENT, which is what makes this decision reversible either way and safe to
# take mid-generation: a row is only written when the search completed, so the ceiling decides WHICH
# positions are labelled and never what a label says. Tightening or loosening it never invalidates
# rows already banked, and a later run at a HIGHER ceiling is purely additive.
#
# Until the deck owner rules, phase A runs at the engine default (1e6 virtual ms) and the pipeline
# stays knob-free. If the clock runs out inside phase A, the supported answer is `finish`, which
# accepts the rows on disk as final and runs B..E on them -- not a quiet ceiling change.

# key | deck-dir | stem | matrix-key | row-seed-base | row-games -- GENERATED, never edited.
#
# scripts/deck_registry.py discovers decks/*/ and derives every path from the folder, so a new deck
# needs no entry anywhere. Row seed bases are distinct multiples of 100k (row-games never approaches
# 100k), so `seed / 100000` recovers the deck when splitting the pooled file; the registry derives
# each from a hash of the deck key rather than from position, so adding or removing a deck cannot
# renumber the others and orphan rows already on disk.
#
# Games OVERSHOOT the ~11k-row knee (~5-6 rows/game measured on hinata and auras) because overshoot
# inside one pooled batch is free -- it adds no tail -- whereas undershooting needs a second batch and
# therefore a second tail.
ROW_GAMES=2500
mapfile -t DECK_TABLE < <(python3 scripts/deck_registry.py --shell "$ROW_GAMES")
[ ${#DECK_TABLE[@]} -gt 0 ] || { echo "no decks discovered under decks/ -- each needs <stem>.cod|.txt AND <stem>.profile.json"; exit 2; }
# NAMED-DECK MODE. `run <deck-dir> [<deck-dir> ...]` replaces the table above with one row per named
# folder, so a NEW deck goes through exactly this pipeline instead of a hand-rolled one. Nothing else
# differs: same phases, same staging, same freeze rule. Its own VLQ dir keeps it from colliding with
# a fleet run.
#
# MORE THAN ONE DECK GOES IN ONE QUEUE, never a deck-at-a-time loop. Every phase here is already
# multi-deck (the fleet path IS this path with more rows), and the reason matters: phases A, C and E
# are each ONE pooled batch, so N decks in one queue pay three tails TOTAL while N sequential runs
# pay 3N -- plus each run's serial stretches (train, metadata, the report pass) idle the whole box
# while the next deck's games wait. That is the repo's standing rule (CLAUDE.md: one pooled work
# queue; "WAVES ARE A LOOP"), and per-deck invocation is exactly the wave it forbids. Pooling costs
# nothing in isolation either -- rows split by seed bucket, cells are keyed by deck, and phase E
# writes per-deck reports.
DECK_DIRS=("${@:2}")
DECK_DIR=${2:-}                                    # first only; the resume hint uses DECK_ARGS below
DECK_ARGS=""
if [ ${#DECK_DIRS[@]} -gt 0 ]; then
    DECK_TABLE=(); _keys=()
    for _dd in "${DECK_DIRS[@]}"; do
        _dd=${_dd%/}
        [ -d "$_dd" ] || { echo "no such deck dir: $_dd"; exit 2; }
        # STEM AND KEY COME FROM THE REGISTRY, not from basename(dir).
        #
        # This used to be `_stem=$(basename "$_dd")`, which silently assumes the folder name IS the
        # file stem. That is true only for a SHIPPING list. A VERSION of a list lives at
        # `decks/<Deck>/v<N>-<slug>/` and its files are named after the PARENT deck -- the layout
        # CLAUDE.md prescribes for an archived or candidate list, and the one the engine needs,
        # because every sidecar is resolved directory-relative off the profile. So on a version
        # folder basename() produced `v2-puresteel-hammer`, this refused with "no profile at
        # .../v2-puresteel-hammer.profile.json", and the whole generation pipeline looked as though
        # it could not address a versioned list at all.
        #
        # It can: deck_registry.discover() has always found these, under the compound key
        # `<parent>_<variant>` (and tools/play/server.js lists them the same way, as
        # "<Deck> -- <variant>"). Only this hand-rolled resolution disagreed with it. Asking the
        # registry for (key, stem) is also what keeps the two from drifting again -- the registry's
        # own docstring is about exactly this failure mode ("a registry that must be edited by hand
        # is a registry that is wrong"), and `--shell` already emits the key|dir|stem row this loop
        # builds.
        _reg=$(python3 scripts/deck_registry.py --shell "$ROW_GAMES" | awk -F'|' -v d="$_dd" '$2==d{print; exit}')
        if [ -z "$_reg" ]; then
            echo "no registry entry for deck dir $_dd"
            echo "  scripts/deck_registry.py discovers decks/<Stem>/ -- and decks/<Stem>/<Variant>/ --"
            echo "  holding <Stem>.cod|.txt AND <Stem>.profile.json (files named after the PARENT deck)."
            exit 2
        fi
        _key=$(printf '%s' "$_reg" | cut -d'|' -f1)
        _stem=$(printf '%s' "$_reg" | cut -d'|' -f3)
        [ -e "$_dd/$_stem.profile.json" ] || { echo "no profile at $_dd/$_stem.profile.json -- run analyze_deck.py first"; exit 2; }
        # REGRESSION-SUITE MEMBERSHIP IS A PRECONDITION (user directive, 2026-09-26). Checked here --
        # per deck, before phase 0 freezes anything and before a single game is played -- because the
        # whole point is an EARLY error. A deck outside the suite has no ground truth, so nothing
        # detects when an engine change breaks the play this leaf is about to be fitted to.
        # MTG_ALLOW_UNTESTED_DECK=1 overrides, and that is a USER decision, never an agent's: an agent
        # that hits this gate reports it and stops, exactly as it would for the cost rule.
        if [ "${MTG_ALLOW_UNTESTED_DECK:-0}" != 1 ]; then
            python3 scripts/suite_gate.py --require "$_dd" >/dev/null || {
                python3 scripts/suite_gate.py --require "$_dd"
                echo "value-leaf generation REFUSED for $_stem (set MTG_ALLOW_UNTESTED_DECK=1 to override -- user only)"
                exit 3
            }
        fi
        # ROW SEED BASE. phase_split recovers which deck a pooled row belongs to with
        # `seed / 100000`, so the bases must be DISTINCT whenever more than one deck shares a queue.
        #   * one deck  -> the historical literal 900000. Every existing single-deck queue's rows on
        #     disk were dumped under it, and moving it would strand them (they would split into an
        #     empty bucket and phase B would retrain on nothing while reporting success).
        #   * two+      -> the registry's assignment, which is the same one the fleet uses: a
        #     crc32-of-key slot, so adding or removing a deck cannot renumber the others.
        if [ ${#DECK_DIRS[@]} -eq 1 ]; then _base=900000
        else _base=$(python3 scripts/deck_registry.py --shell "$ROW_GAMES" | awk -F'|' -v k="$_key" '$1==k{print $5}'); fi
        [ -n "$_base" ] || { echo "$_key has no registry entry -- scripts/deck_registry.py discovers decks/<Stem>/ holding <Stem>.cod|.txt AND <Stem>.profile.json"; exit 2; }
        DECK_TABLE+=("$_key|$_dd|$_stem|$_key|$_base|$ROW_GAMES")
        _keys+=("$_key")
        DECK_ARGS="$DECK_ARGS $_dd"
    done
    # Queue dir keyed by the SORTED key set, so `run A B` and `run B A` resume the same queue rather
    # than silently starting a second one that repeats every game.
    VLQ=logs/vlq_$(printf '%s\n' "${_keys[@]}" | sort | paste -sd+ -)
    # A long key set overflows NAME_MAX (255, + "logs/"): mkdir fails and the run cannot start. Past it, name the queue by
    # a hash of the same sorted set -- still order-independent and resumable; every shorter name is unchanged.
    [ ${#VLQ} -le 260 ] || VLQ=logs/vlq_${#_keys[@]}decks_$(printf '%s\n' "${_keys[@]}" | sort | sha1sum | cut -c1-12)
    DONE=$VLQ/done; ROWDIR=$VLQ/rows
    mkdir -p "$DONE" "$ROWDIR" logs/eval
fi

ALL_ROWS=$ROWDIR/all.rows
MATRIX_TXT=$VLQ/matrix.txt          # per-run: a single-deck run must not overwrite the fleet table
LEAFCAL=$VLQ/leafcal.json           # phase C0: per-deck greedy-leaf threshold for the H cells
# Derived AFTER single-deck mode has finalised VLQ. Set beside SLOW_GAME_MS above, it captured the
# fleet default (logs/vlq/slow_games.log) and every single-deck run appended to the same shared file
# -- the FiveColour run's 17 slow games landed there while its own queue dir showed none.
SLOW_GAME_LOG=$VLQ/slow_games.log
# Decisions a phase took WITHOUT stopping (never block on a question): automatic adoptions, and the
# default taken on any question put to a human (with what to do instead). Printed by `status` and at
# COMPLETE until a human deletes the file.
DECISIONS=$VLQ/DECISIONS.txt

log() { echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$VLQ/driver.log"; }
done_p() { [ -e "$DONE/$1" ]; }
mark()   { date '+%Y-%m-%dT%H:%M:%S' > "$DONE/$1"; }
deck_file() { ls "$1/$2".cod "$1/$2".txt 2>/dev/null | head -1; }

src_fingerprint() { git rev-parse HEAD:src 2>/dev/null; }

# DECKLIST fingerprint: `<key> <sha256 of the decklist>` per deck in the table, sorted.
#
# The src/play freeze above is entirely about ENGINE state and is blind to the deck actually being
# measured -- but every row, matrix cell and trained model is fitted to ONE decklist, so a list that
# changes under a queue invalidates all of them. This is not hypothetical: the queue dir is
# `logs/vlq_<key>` and the staged model `logs/eval/<stem>.value.STAGED.json`, both keyed by the deck
# FAMILY, so replacing a deck's shipping list points a fresh run straight at the previous list's
# completed run -- whose done-markers make `run` skip every phase and hand back a "finished" model
# measured on a deck we no longer ship. The play digest cannot catch it either: a swapped deck is
# usually still pinned to its OLD list in the smoke suite (deliberately, until its artifacts exist),
# so the digest does not move at all. Caught on Mirrorwing 2026-08-22.
#
# LINE ENDINGS ARE STRIPPED BEFORE HASHING, and that is not cosmetic. A .cod is Cockatrice's format
# and routinely arrives CRLF; git hands the working tree LF. So a checkout, a rebase, or an editor
# round-trip flips every byte of the file without changing one card, and a raw-byte hash calls that
# a new decklist. That is the WRONG failure direction for this particular guard: its documented
# response is "there is nothing to keep, archive the queue and start over", so a whitespace-only
# normalisation costs hours of banked rows. Caught on Fungus 2026-09-18, where a rebase re-materialised
# Fungus.cod as LF and the stamp (taken when the tree held CRLF) declared the list replaced -- with
# 6885 labelled rows behind it, and the file provably identical card-for-card.
deck_fingerprint() {
    local row key dir stem df
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem _ <<<"$row"
        df=$(deck_file "$dir" "$stem")
        [ -n "$df" ] && printf '%s %s\n' "$key" "$(tr -d '\r' < "$df" | sha256sum | cut -d' ' -f1)"
    done | sort
}

# The pre-2026-09-18 RAW-BYTE fingerprint, in whichever line-ending form is named: `lf` for the file
# exactly as it sits, `crlf` for the same cards with CRLF endings. Kept ONLY so a queue stamped by the
# old function migrates instead of aborting -- the stamp is a bare hash, so the only way to ask "were
# these the same cards?" after the fact is to re-hash the CURRENT content in the form the stamp used.
deck_fingerprint_legacy() {
    local mode=$1 row key dir stem df
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem _ <<<"$row"
        df=$(deck_file "$dir" "$stem")
        [ -n "$df" ] || continue
        if [ "$mode" = crlf ]; then
            printf '%s %s\n' "$key" "$(tr -d '\r' < "$df" | sed 's/$/\r/' | sha256sum | cut -d' ' -f1)"
        else
            printf '%s %s\n' "$key" "$(sha256sum "$df" | cut -d' ' -f1)"
        fi
    done | sort
}

# A decklist change is a HARD STOP, unlike a src/play change. There is no chunk rule to fall back on:
# a play change leaves earlier games VALID (they measured the same deck on an interchangeable engine)
# so full sets can be banked, whereas a different list makes every row, cell and model describe a
# different deck -- there is nothing to keep. It also refuses to auto-wipe: hours of work belonging to
# the previous list are not this script's to discard, and which list they belong to is the operator's
# call. Archive or delete the queue deliberately, then re-run.
check_decks() {
    local frozen now
    frozen=$VLQ/freeze.decks
    [ -e "$frozen" ] || return 0            # pre-dates the stamp; nothing to compare against
    now=$(deck_fingerprint)
    [ "$now" = "$(cat "$frozen")" ] && return 0
    # A queue stamped before the line-ending normalisation above. If the CURRENT file re-hashes to the
    # stamp in either raw form, the cards never changed and only the stamp's FORM is stale -- re-stamp
    # and carry on rather than throwing the queue away.
    for _form in lf crlf; do
        [ "$(deck_fingerprint_legacy "$_form")" = "$(cat "$frozen")" ] || continue
        log "decklist stamp is the pre-2026-09-18 raw-byte form ($_form); the list is UNCHANGED"
        log "  card-for-card (line endings only) -- re-stamping and continuing."
        printf '%s\n' "$now" > "$frozen"
        return 0
    done
    log "ABORT: the DECKLIST changed under this queue -- every row, matrix cell and trained model in"
    log "  $VLQ is fitted to the previous list and describes a deck that is no longer being measured."
    log "  Unlike a src/play move there is nothing to salvage, so this stops rather than resuming."
    diff <(cat "$frozen") <(printf '%s\n' "$now") | sed 's/^/    /' | while read -r l; do log "$l"; done
    log "  Archive the old queue and re-run, e.g.:"
    log "    mv $VLQ ${VLQ}_<old-list-name>"
    log "    mv 'logs/eval/<stem>.value.STAGED.json' 'logs/eval/<stem>.<old-list-name>.value.STAGED.json'"
    return 1
}

# LEAF-SHAPE fingerprint: `<key> <json of the profile's search_leaf_* keys>` per deck, sorted -- the
# working tree's profiles, or (with a rev) the profiles as committed at that rev.
#
# Those keys set the horizon leaf the H cells are scored with (search_leaf_depth,
# search_leaf_first_turn_depth), so changing one mid-run puts H cells measured on two different leaves
# into one table. NEITHER other guard is guaranteed to see it: the decklist stamp hashes the list, not
# the profile, and the play fingerprint is budgeted d3 probes plus smoke digests, which a change that
# only bites unbudgeted deep passes leaves byte-identical (a greedy leaf for passes of depth >= 3 moved
# 0 of ~1,140 budgeted Snow games while cutting its unbudgeted H3-H5 cells 14-60x). check_freeze would
# then report PLAY UNCHANGED and keep every banked chunk -- the exact mix this stops. 2026-10-14, Snow.
# (Phase C0's per-deck thresholds are not a profile key; the matrix driver stamps those itself.)
leaf_fingerprint() {
    local rev=${1:-} row key dir stem f top
    top=$(git rev-parse --show-toplevel)
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem _ <<<"$row"
        f="$dir/$stem.profile.json"
        if [ -n "$rev" ]; then git show "$rev:$(realpath -m --relative-to="$top" "$f")" 2>/dev/null
        else cat "$f" 2>/dev/null; fi | python3 -c '
import json, sys
try:    p = json.load(sys.stdin)
except Exception: p = {}
print(sys.argv[1], json.dumps({k: v for k, v in p.items() if k.startswith("search_leaf")}, sort_keys=True))' "$key"
    done | sort
}

# A leaf-shape change is a HARD STOP, like a decklist change, but for the opposite reason: here most of
# the run IS salvageable (V cells never read the rollout leaf, and an H cell shallower than the rule's
# threshold is untouched), and which cells to move aside is the operator's call, not this script's.
# A queue frozen before this stamp existed is backfilled from the profiles at its frozen commit, so the
# guard covers runs already in flight.
check_leaf_keys() {
    local frozen=$VLQ/freeze.leaf now
    if [ ! -e "$frozen" ]; then
        [ -s "$VLQ/freeze.commit" ] || return 0      # nothing frozen yet; phase_freeze stamps it
        leaf_fingerprint "$(cat "$VLQ/freeze.commit")" > "$frozen"
        log "leaf-shape stamp backfilled from the profiles at $(cat "$VLQ/freeze.commit")"
    fi
    now=$(leaf_fingerprint)
    [ "$now" = "$(cat "$frozen")" ] && { rm -f "$frozen.now"; return 0; }
    printf '%s\n' "$now" > "$frozen.now"
    log "ABORT: a deck profile's search_leaf_* keys changed under this queue, so its H cells would mix two"
    log "  horizon leaves. The play fingerprint cannot see this (budgeted play is unchanged by design)."
    diff <(cat "$frozen") <(printf '%s\n' "$now") | sed 's/^/    /' | while read -r l; do log "$l"; done
    log "  Move aside the matrix chunks of every H cell the new keys change (search_leaf_depth: every H cell;"
    log "  search_leaf_first_turn_depth: every H cell) and the skip list they fed, then accept:"
    log "    mv $frozen.now $frozen"
    log "  See docs/design/snow-matrix-leaf-fidelity.md section 10."
    return 1
}

# PLAY fingerprint: the smoke suite's per-case play digests, folded to one hash.
#
# What the freeze is actually protecting is that every cell of the table was measured by the same
# PLAY. `git rev-parse HEAD:src` is only a proxy for that, and it over-triggers badly: a comment, a
# docs block under src/, a card-data edit, or an opt-in flag that is byte-identical all move the tree
# hash and invalidate a table they cannot possibly have affected. That is not hypothetical -- it cost
# this pipeline a run on 2026-08-12, twice, for changes the suite proved play-neutral (33/33 configs,
# 0 games changed). If play does not change there is nothing to worry about (user, 2026-08-12).
#
# The digests in test/results/smoke.env ARE the repo's play-identity instrument: each is a fold of
# per-game decision-stream hashes over 33 configs spanning every suite deck at d0/d3/d5, and the
# regression harness already treats a digest move as a play change even when the average is identical.
# Folding them gives a single value that is stable across machines and thread counts (every run is
# deterministic and thread-invariant) and changes iff play changed within that coverage.
#
# COVERAGE IS THE LIMIT, and it is a sample, not a proof: a play change confined to a deck or depth
# the smoke matrix does not exercise would not show up here. So this is only ever used to ACCEPT a
# src move that the suite says is play-neutral -- never to reject one, and never to skip the src check
# when the fingerprint is missing. Failing that way round means the worst case is the old behaviour.
# Per-QUEUE-DECK play probe, folded into the fingerprint below (2026-09-06). The smoke fold's
# coverage is the SUITE -- and the deck a generation queue holds is exactly the deck most likely
# NOT to be in it (melira was pulled from the suite for cost 2026-09-06; its bottoming policy then
# changed its play and the smoke fold could not see it, so a resume would have kept 2500 games of
# rows dumped under play we no longer ship). Probe: 24 deterministic d3/b10 games per queue deck at
# the deck's own row seed base, content-hashed from their logs (runId stripped; log content is
# deterministic per seed and thread-invariant). d3/b10 is a FIXED probe config, not the dump
# config: its job is sensitivity at bounded cost, and it catches the mulligan/bottoming class
# (manifests at every depth) plus ordinary play movement. A change confined to depths/paths the
# probe does not walk can still slip past -- same "accept only, sample not proof" stance as the
# smoke fold this extends.
#
# KNOWN LIMIT (recorded, not yet built): on PLAY CHANGED the chunk banking repairs the MATRIX, but
# phase A's game-level resume keeps every game that already banked a row -- rows cannot be
# partially re-derived, so a real row fix means moving rows/all.rows aside and re-dumping the
# affected deck. Do that by hand when this fingerprint (or any other evidence) says the queue
# deck's own play moved; auto-purge is withheld because on a FLEET queue it would nuke every
# deck's rows for one deck's change.
deck_play_probe() {
    local row key dir stem mkey base games tmp out
    tmp=$(mktemp -d) || return 1
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        mkdir -p "$tmp/$key"
        "$(harness_bin)" "$(deck_file "$dir" "$stem")" --profile "$dir/$stem.profile.json" \
            --ignore-play-profile --depth 3 --budget-ms 10 \
            --games 24 --seed "$base" --threads 0 --log-dir "$tmp/$key" \
            >/dev/null 2>&1 || { rm -rf "$tmp"; return 1; }
        out=$(python3 - "$tmp/$key" <<'PY'
import json, glob, hashlib, sys
h = hashlib.sha256()
files = sorted(glob.glob(sys.argv[1] + "/*_game_*.json"))
if not files: sys.exit(1)
for f in files:
    g = json.load(open(f)); g.pop("runId", None)
    h.update(json.dumps(g, sort_keys=True).encode())
print(h.hexdigest())
PY
        ) || { rm -rf "$tmp"; return 1; }
        printf '%s %s\n' "$key" "$out"
    done
    rm -rf "$tmp"
}
play_fingerprint() {
    bash test/regression.sh --smoke >/dev/null 2>&1 || true   # digests are recorded even on FAIL
    [ -f test/results/smoke.env ] || return 1
    local probe
    probe=$(deck_play_probe) || return 1
    # <key>=<avg>/<digest> -- keep the digest half only, so a pure timing/avg difference cannot move
    # this and a digest difference always does. Sorted so the fold is order-independent. The queue
    # decks' own probe hashes fold in after (see deck_play_probe).
    { sed -n 's/.*=\([0-9.]*\)\/\([0-9a-f]*\)$/\2/p' test/results/smoke.env | sort
      printf '%s\n' "$probe"; } | sha256sum | cut -d' ' -f1
}

check_freeze() {
    local now frozen play_now play_frozen
    now=$(src_fingerprint); frozen=$(cat "$VLQ/freeze.src" 2>/dev/null || echo "")
    if [ -z "$frozen" ] || [ "$now" = "$frozen" ]; then return 0; fi

    # src moved. NEITHER outcome throws the run away (user, 2026-08-12) -- the two differ only in how
    # much is kept:
    #
    #   PLAY UNCHANGED -> keep EVERYTHING. The measurements are interchangeable, so the chunks are
    #     re-stamped to the new src and the matrix driver's resync sees nothing to absorb.
    #   PLAY CHANGED   -> keep the FULL SETS and drop the rest. Fall through to the driver, whose
    #     resync_engine_change keeps offsets below B (the completed common prefix -- every cell of the
    #     seed has them) and re-queues everything above it. That is the chunk rule the skill already
    #     documents; the blanket stop here was overriding it, which is what made a play change cost the
    #     whole run instead of one level. Set-completion ordering is what makes B worth keeping.
    play_frozen=$(cat "$VLQ/freeze.play" 2>/dev/null || echo "")
    log "src/ moved ($(echo "$frozen" | cut -c1-12) -> $(echo "$now" | cut -c1-12)); checking whether PLAY moved..."
    play_now=$(play_fingerprint || echo "")
    if [ -n "$play_frozen" ] && [ -n "$play_now" ] && [ "$play_now" = "$play_frozen" ]; then
        log "  PLAY UNCHANGED (smoke digests fold to $(echo "$play_now" | cut -c1-12)) -- keeping every game."
        # Re-stamp the CHUNKS too, not just the freeze: they were produced by a play-identical engine,
        # so leaving them on the old src would make the driver's resync drop them for no reason.
        python3 - "$MATRIX_TXT.cells.json" "$now" <<'PY' 2>/dev/null || true
import json, sys
p, new = sys.argv[1], sys.argv[2]
try:    cells = json.load(open(p))
except Exception: sys.exit(0)
for c in cells:
    for ch in c.get("chunks", []): ch["src"] = new
json.dump(cells, open(p, "w"))
PY
    else
        log "  PLAY CHANGED ($(echo "${play_frozen:-unstamped}" | cut -c1-12) -> $(echo "${play_now:-unavailable}" | cut -c1-12)) -- keeping"
        log "  the completed FULL SETS and re-running everything above them (see resync_engine_change)."
    fi
    src_fingerprint > "$VLQ/freeze.src"; git rev-parse --short HEAD > "$VLQ/freeze.commit"
    if [ -n "$play_now" ]; then printf '%s\n' "$play_now" > "$VLQ/freeze.play"; fi
    return 0
}

# Scratch deck folder differing from the real one ONLY in its value.json; siblings are symlinked so the
# ~600 MB keep-model caches are not copied. Verified byte-identical play when the value.json matches.
make_variant_deck() {   # dest src-dir stem value.json
    local dest=$1 src=$2 stem=$3 val=$4 f b
    rm -rf "$dest"; mkdir -p "$dest"
    for f in "$src"/*; do
        b=$(basename "$f"); [ "$b" = "$stem.value.json" ] && continue
        ln -sf "$(realpath "$f")" "$dest/$b"
    done
    # A missing/empty source means NO sidecar -- which is exactly the live arm for a deck that has
    # never had one. Placing nothing is correct: sidecar PRESENCE alone activates the depth-aware
    # hybrid in play, so copying an empty file would silently make the "live" arm the staged arm.
    [ -s "$val" ] && cp "$val" "$dest/$stem.value.json"
    return 0
}

# ------------------------------------------------------------------------- phase 0: freeze + build
phase_freeze() {
    done_p 00_freeze && return 0
    if ! git diff --quiet -- src/ || ! git diff --cached --quiet -- src/; then
        log "ABORT: uncommitted changes under src/ -- the frozen commit would not describe the binary."
        return 1
    fi
    src_fingerprint > "$VLQ/freeze.src"; git rev-parse --short HEAD > "$VLQ/freeze.commit"
    # Which DECK(s) this queue measures -- see deck_fingerprint. Stamped here so every later `run`
    # can refuse to resume a queue whose decklist has been replaced.
    deck_fingerprint > "$VLQ/freeze.decks"
    leaf_fingerprint > "$VLQ/freeze.leaf"
    log "FROZEN at $(cat "$VLQ/freeze.commit")  src-tree $(cut -c1-12 "$VLQ/freeze.src")"
    log "decklist(s) $(wc -l < "$VLQ/freeze.decks" | tr -d ' ') stamped -> $VLQ/freeze.decks"
    bash build.sh >> "$VLQ/build.log" 2>&1 || { log "ABORT: build failed, see $VLQ/build.log"; return 1; }
    log "build OK"
    # Stamp the PLAY fingerprint alongside the src one, so a later src move can be judged on whether
    # it changed play rather than on whether it touched a file. Recorded AFTER the build so it
    # describes the binary the run will actually use. Best-effort: without it check_freeze simply
    # keeps the old src-only behaviour.
    if pf=$(play_fingerprint); then
        printf '%s\n' "$pf" > "$VLQ/freeze.play"
        log "play fingerprint $(echo "$pf" | cut -c1-12) (smoke digests)"
    else
        log "WARN: could not record a play fingerprint; a src move will stop the run as before"
    fi
    mark 00_freeze
}

# ------------------------------------------------------- PHASE A: every deck's rows, ONE pooled batch
phase_rows() {
    done_p A_rows && return 0
    local key dir stem mkey base games row b n
    # GAME-LEVEL RESUME. Rows are durable and dedupe on (seed,turn), but that alone only makes a
    # re-run CORRECT, not cheap: the manifest used to be rebuilt unconditionally, so an interrupted
    # phase A re-played every game, banked or not. On a cheap deck that is noise; on Mirrorwing the
    # 2026-08-13 OOM would have re-paid ~100 core-hours it already held rows for. So: a game that has
    # produced AT LEAST ONE row is skipped, and only the missing games are queued. The known cost is
    # a game KILLED MID-PLAY: its already-banked early turns count it as done, so its unwritten
    # late-turn rows are lost -- a handful of rows against a hundred core-hours; `finish` exists
    # precisely because fewer rows is acceptable.
    #
    # Fidelity: a re-run game must reproduce the original chunk scheme exactly. Chunk b gave its
    # game k (absolute in [b,b+250)) shuffle seed base+k and global game index k-b, so a missing
    # range [k, k+n) re-runs as {seed: base+k, game_index: k-b, games: n}, split at chunk
    # boundaries -- byte-identical to the games the original chunk would have played.
    { for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        awk -v base="$base" -v games="$games" '
            !/^#/ { s = $(NF-1) - base; if (s >= 0 && s < games) seen[s] = 1 }
            END   { st = -1
                    for (i = 0; i <= games; i++) {
                        miss = (i < games && !(i in seen))
                        if (miss && st < 0) { st = i }
                        # flush at a run end or a 250-game chunk boundary (gi is chunk-relative)
                        if (st >= 0 && (!miss || i % 250 == 0) && i > st) {
                            print st, i - st; st = miss ? i : -1
                        }
                    } }' "$( [ -f "$ALL_ROWS" ] && echo "$ALL_ROWS" || echo /dev/null )" \
        | while read -r st n; do
            b=$(( st / 250 * 250 ))
            h_job "${key}_rows_$(printf '%05d' "$b")_$(printf '%03d' "$(( st - b ))")" \
                  "$(deck_file "$dir" "$stem")" "$dir/$stem.profile.json" \
                  "$n" "$(( base + st ))" "game_index=$(( st - b ))"
        done
      done; } | h_manifest "$ALL_ROWS.manifest.json" >/dev/null || { log "PHASE A: no games missing -- all rows banked"; mark A_rows; return 0; }
    # Counted from the manifest, not accumulated in the loop: the loop runs inside the pipeline's
    # SUBSHELL, so a variable incremented there is lost and the line logged "0 games".
    log "PHASE A: $(grep -c '"name"' "$ALL_ROWS.manifest.json") jobs across ${#DECK_TABLE[@]} decks in ONE queue (K=$ROW_K searched labels)."
    log "  One tail, at the very end. Hinata dominates; the other decks fill cores behind its slow games."
    # --threads 0 = let the engine resolve to every logical CPU it is actually allowed to use
    # (affinity-aware), exactly as phases C and E already do. The old literal 24 was the DEV box's
    # core count baked in as a number -- on a 32-thread machine it silently left a quarter of the
    # box idle through the most expensive phase.
    # The exit status MUST gate the marker. On 2026-09-05 the kernel OOM-killed this batch 375
    # games into 2500 and the unchecked invocation fell through to `mark A_rows`: phase B trained
    # on 14% of the rows and phase C started measuring that model as if it were real. Rows are
    # banked, so a failed batch costs nothing -- but a MARKED failed batch poisons every later
    # phase. On failure: no marker, loud log, and the next `run` re-queues only the missing games.
    # MTG_VLQ_ROWS_THREADS (default 0 = all cores): phase-A-only concurrency lever, added for the
    # 2026-09-05 Melira OOMs. The label path's UNBOUNDED search has a per-game transient (combo-turn
    # enumeration) that spiked the batch 5 GB -> 23 GB in under 8 minutes on 32 workers -- a class
    # no cache cap bounds, and per-game footprint is fine solo (a 98 s game peaks 64 MB). Peak
    # transient scales with concurrent monster games, so fewer workers is the safe lever on a small
    # box until the enumeration transient itself is bounded engine-side.
    log "  per-game wall backstop: hard cap ${ROWS_WALL_SEC}s$( [ "$ROWS_PREDICT_SEC" -gt 0 ] && echo ", predictive cut ${ROWS_PREDICT_SEC}s" || echo " (no predictive stage -- user ruling)" )"
    # The env prefix MUST stay contiguous with the binary. 0ec772c9 put a log line between a prefix
    # and its generator, so the prefix applied to `[` and the run executed with no ceiling at all.
    MTG_DUMP_VALUE_ROWS="$ALL_ROWS" MTG_EVAL_ROWS_K="$ROW_K" MTG_EVAL_ROWS_ROLLOUT=0 \
    MTG_MAX_GAME_PREDICT_SEC="$ROWS_PREDICT_SEC" MTG_MAX_GAME_WALL_SEC="$ROWS_WALL_SEC" \
        "$(harness_bin)" --batch "$ALL_ROWS.manifest.json" --threads "${MTG_VLQ_ROWS_THREADS:-0}" \
        > "$VLQ/rows.batch.log" 2>&1 \
        || { local rc=$?
             log "PHASE A: batch FAILED (exit $rc -- 137=SIGKILL, think OOM). Rows on disk are"
             log "  banked and safe; NOT marking A_rows. Re-run to queue only the missing games."
             return 1; }
    # SLOW-GAME AUDIT -- gates the marker, same discipline as the batch exit status above.
    # Counting games at/over HALF the cap rather than only at it: a game cut AT the cap may not report
    # its own line, and "this deck generates 2-hour label games" is the optimisation signal regardless
    # of where the cap happens to sit. NOT inferred from `queued - sum(played=)`: that reads a 500-game
    # shortfall when the true count is 0, because a job stopped mid-chunk never prints a played= line
    # at all -- conflating the two holes BatchRunner.h warns "mean opposite things".
    local _half=$(( ROWS_WALL_SEC / 2 )) _queued _slow _pct
    _queued=$(python3 -c "import json,sys; print(sum(j['games'] for j in json.load(open(sys.argv[1]))['jobs']))" \
              "$ALL_ROWS.manifest.json" 2>/dev/null || echo 0)
    _slow=$(grep -ho "SLOW-GAME [0-9]*ms" "$VLQ/rows.batch.log" 2>/dev/null \
            | sed -E 's/[^0-9]*([0-9]+)ms/\1/' | awk -v t="$((_half*1000))" '$1>=t' | wc -l)
    log "PHASE A: $_slow/$_queued games ran >= ${_half}s (half the ${ROWS_WALL_SEC}s cap)"
    if [ "${ROWS_SLOW_FAIL_PCT:-0}" -gt 0 ] && [ "$_queued" -gt 0 ]; then
        _pct=$(( _slow * 100 / _queued ))
        if [ "$_pct" -ge "$ROWS_SLOW_FAIL_PCT" ]; then
            log "PHASE A: FAILING OUT -- ${_pct}% of games are super slow (>= ${_half}s), at or over the"
            log "  ${ROWS_SLOW_FAIL_PCT}% bar. This is an ENGINE-OPTIMISATION signal, not a cap to loosen:"
            log "  a bigger cap would only hide it, and a leaf fitted to a truncated row set is fitted to"
            log "  games we cannot afford to play. Rows on disk are banked and safe; NOT marking A_rows."
            log "  Optimise, then re-run (rows are additive) -- or accept them deliberately with:"
            log "    bash scripts/valueleaf.sh finish <deck-dir>"
            return 1
        fi
    fi
    log "PHASE A done: $(grep -vc '^#' "$ALL_ROWS" 2>/dev/null || echo 0) rows total"
    mark A_rows
}

# Split the pooled file by seed range, then dedupe + sort per deck. Dedupe is on (seed,turn) -- the last
# two fields, keyed off NF so adding a feature to the row cannot silently break it. Sorting matters
# because the dump is multi-threaded: row order varies run to run, and an unsorted training file makes
# training irreproducible.
phase_split() {
    done_p A_split && return 0
    local key dir stem mkey base games row hdr bucket
    hdr=$(grep -m1 '^#' "$ALL_ROWS")
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        bucket=$(( base / 100000 ))
        { echo "$hdr"
          awk -v b="$bucket" '!/^#/ && int($(NF-1)/100000)==b' "$ALL_ROWS" \
            | awk '!seen[$(NF-1)" "$NF]++' | sort
        } > "$ROWDIR/$stem.rows"
        log "  split $key: $(grep -vc '^#' "$ROWDIR/$stem.rows") unique rows"
    done
    mark A_split
}

# ------------------------------------------- PHASE B: train each deck's model (no games, so no tail)
# Merges ONLY eval_model into a COPY of the live sidecar, so value_play / the old table / the old
# crossover survive until the new table is measured and the A/B has run. A deck that fails here is
# skipped, not fatal -- the rest of the fleet still completes.
phase_train() {
    done_p B_train && return 0
    local key dir stem mkey base games row src staged n
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        done_p "B_train_$key" && continue
        src=$ROWDIR/$stem.rows; staged=logs/eval/$stem.value.STAGED.json
        n=$(grep -vc '^#' "$src" 2>/dev/null || echo 0)
        if [ "$n" -lt 1000 ]; then log "  $key SKIPPED: only $n rows"; continue; fi
        log "  training $key on $n rows"
        python3 scripts/attic/train_eval_gbdt.py --rows "$src" --out "$staged.raw" \
            --regression --trees 120 --depth 4 --lr 0.15 --min-leaf 20 >> "$VLQ/train_$key.log" 2>&1
        if [ ! -s "$staged.raw" ]; then log "  $key SKIPPED: trainer produced nothing"; continue; fi
        python3 - "$dir/$stem.value.json" "$staged.raw" "$staged" "$(cat "$VLQ/freeze.commit")" "$n" <<'PY'
import json, os, sys
live, raw, out, commit, n = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
R = json.load(open(raw))
if os.path.exists(live):
    L = json.load(open(live))          # REgeneration: keep value_play/table/crossover until measured
    L["eval_model"] = R["eval_model"]
else:
    L = R                              # FIRST model for this deck: nothing prior to preserve
L.setdefault("provenance", {}).update({
    "regenerated_commit": commit, "rows": n,
    "note": "eval_model retrained on rows dumped at SHIPPED play WITH the deck profile, so the "
            "exhaustive keep model was live. Labels are searched K=3 for every deck (a pooled dump "
            "forces one label mode). value_leaf_table/crossover here are the OLD ones until the "
            "regenerated matrix lands.",
})
json.dump(L, open(out, "w"), indent=1)
PY
        rm -f "$staged.raw"; mark "B_train_$key"
    done
    mark B_train
}

# ------------------------------------ PHASE C: every deck's matrix cells, ONE work-stealing pool
# Incremental, batched, tractability-aware: cells advance in 25-game batches submitted round-robin into
# a work-stealing pool, so a cell stuck on a long game holds ONE worker while the rest keep churning,
# and a row whose MEDIAN game is slower than --intractable-median-sec-per-game is capped at a reference
# sample rather than burning
# the box on a cell no production run could use. Passing every deck at once means one pool and one tail
# instead of one per deck. Run with the box to ITSELF: the cutoff is wall-clock based, so a loaded box
# misclassifies slow cells as intractable and silently truncates the table.
staged_keys() {
    local row key dir stem mkey base games out=""
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -s "logs/eval/$stem.value.STAGED.json" ] && out="$out ${mkey}_staged"
    done
    echo "$out"
}
# --------------------------------------- PHASE C0: LEAF CALIBRATION (per deck, before the matrix)
# Which H cells may score their horizon with a greedy playout instead of the 1-ply leaf. The 1-ply leaf
# is nearly all of a deep H cell's work on a wide deck (Snow: ~116 candidates per simulated turn, each
# rolled out; its 2026-10-13 run hit the per-game ceiling on 18% of games, almost all at H4/H5), while
# on combo decks it decides real games (fleet, unbudgeted: H3 9 of ~490 and H4 3 of 470 worse under a
# greedy leaf, all on decks whose greedy playout misses the kill). No single threshold is free on every
# deck, so it is MEASURED per deck here (USER 2026-10-14, option 2 of three): paired H3..Hmax games,
# 1-ply vs greedy, ONE pool; K = the shallowest depth from which every cell moved no win turn ("0 is
# neutral"). The matrix's H cells of depth >= K then use the greedy leaf; a deck with no clean cell
# keeps the 1-ply leaf everywhere. Needs the staged model (the H cells' warm-up passes run on it), so it
# sits after phase B. Never stops the run: the verdict is recorded in $DECISIONS like every other call
# this script makes on its own. Mechanism and evidence: run_leafcal in the matrix driver,
# docs/design/snow-matrix-leaf-fidelity.md.
phase_leafcal() {
    done_p C0_leafcal && return 0
    local keys; keys=$(staged_keys)
    [ -n "$keys" ] || { log "PHASE C0 ABORT: no staged models"; return 1; }
    log "PHASE C0: leaf calibration over$keys -- paired H cells >= 3, 1-ply vs greedy leaf, ONE pool"
    python3 scripts/attic/valueleaf_depth_matrix.py --leafcal "$LEAFCAL" --decks $keys --hdepths $HDEPTHS \
        --max-game-predict-sec "$MAX_GAME_PREDICT_SEC" --max-game-wall-sec "$MAX_GAME_WALL_SEC" \
        > "$VLQ/leafcal.log" 2>&1 \
        || { log "PHASE C0 ABORT: calibration pool failed (see $VLQ/leafcal.log) -- NOT marked, re-run to retry"; return 1; }
    [ -s "$LEAFCAL" ] || { log "PHASE C0 ABORT: no $LEAFCAL written"; return 1; }
    while IFS= read -r l; do log "  $l"; done < "$VLQ/leafcal.log"
    { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE C0 leaf calibration (taken without stopping)"
      echo "    rule: the matrix's H cells of depth >= K use the greedy leaf, K = the shallowest depth from"
      echo "    which every paired cell moved no win turn; no K = the 1-ply leaf everywhere (a deck whose profile"
      echo "    pins search_leaf_depth plays that leaf in every cell instead -- nothing to calibrate)."
      sed 's/^/    /' "$VLQ/leafcal.log"; echo; } >> "$DECISIONS"
    mark C0_leafcal
}

phase_matrix() {
    done_p C_matrix && return 0
    local keys; keys=$(staged_keys)
    [ -n "$keys" ] || { log "PHASE C ABORT: no staged models"; return 1; }
    log "PHASE C: matrix over$keys -- ONE pool, all available cores, target $MATRIX_TARGET/cell, H=[$HDEPTHS], V=[$VDEPTHS], cutoff median ${INTRACTABLE_MEDIAN_SPG}s/game, never-condemn<=$NEVER_CONDEMN, per-game ceiling ${ABANDON_K}x median of first $ABANDON_CALIB"
    log "  wall-clock backstop: predictive cut at $((MAX_GAME_PREDICT_SEC/60)) min, hard cap at $((MAX_GAME_WALL_SEC/60)) min -- a FAILSAFE for when the unit ceiling stops bounding wall clock, not a cost control"
    log "  lazy leaf: ARMED on the H arm (MTG_LAZY_LEAF=1, set per-cell by the driver). One leafless"
    log "    full-depth pass before the ladder; answer-identical (per-game win turns verified), worth"
    log "    -10.4% at d4 / -36.8% at d5 in units on Snow. A MEDIAN-game lever -- the tail is the"
    log "    backstop's job, not this one. Self-disables on any budgeted cell."
    # The per-deck greedy-leaf thresholds from phase C0 (absent on a queue that predates it: no rule).
    local LEAFCAL_ARG=()
    [ -s "$LEAFCAL" ] && LEAFCAL_ARG=(--leaf-greedy-from "$LEAFCAL")
    MTG_SLOW_GAME_LOG="$SLOW_GAME_LOG" \
    python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks $keys \
        --hdepths $HDEPTHS --vdepths $VDEPTHS --seeds 8008 9009 10010 11011 \
        --target "$MATRIX_TARGET" --reference-target "$MATRIX_REF" --batch 25 \
        --value-min-depth 0 --intractable-median-sec-per-game "$INTRACTABLE_MEDIAN_SPG" \
        --never-condemn-at-or-below "$NEVER_CONDEMN" \
        --abandon-k "$ABANDON_K" --abandon-calib "$ABANDON_CALIB" \
        --abandon-floor-units "$ABANDON_FLOOR_UNITS" \
        --abandon-units "$ABANDON_FLOOR_UNITS" \
        --max-game-predict-sec "$MAX_GAME_PREDICT_SEC" \
        --max-game-wall-sec "$MAX_GAME_WALL_SEC" \
        ${LEAFCAL_ARG[@]+"${LEAFCAL_ARG[@]}"} \
        --out "$MATRIX_TXT" >> "$VLQ/matrix.log" 2>&1
    local mrc=$?
    # The driver exits 3 when the pooled batch died before reporting every chunk (Snow 2026-09-22:
    # the pool was gone after 63 min with 32 games in flight, and this phase was marked done over a
    # 51-game table, which then starved D of rows and shipped a sidecar with no value_play at all).
    # Banked chunks stay on disk; a re-run resumes. Marking done here would be the one thing that
    # could not be undone by a resume.
    [ "$mrc" = 0 ] || { log "PHASE C ABORT: matrix driver exit $mrc (pool died early? see $VLQ/matrix.log) -- NOT marked done, re-run to resume"; return 1; }
    [ -s "$MATRIX_TXT" ] || { log "PHASE C ABORT: no matrix output"; return 1; }
    log "PHASE C done"
    mark C_matrix
}

# --------------------------------------------------- PHASE D: table -> metadata (no games, no tail)
# ------------------------------- PHASE C.5: can the ABANDONED games have changed the table's answers?
# The matrix reports "games that COMPLETE within the ceiling", not all games. That is usually
# harmless, but it is not harmless BY CONSTRUCTION, and every completed game is retained on disk, so
# the question is answerable for free. See scripts/matrix_risk_gate.py.
#
# It does NOT auto-raise the ceiling. Three outcomes with different remedies: a filter-decided verdict
# wants more CEILING, a noise-dominated one wants more GAMES, and a pile of condemnations is an
# ENGINE-optimisation signal that a bigger ceiling would only hide (user: "If we have a ton of
# condemnations it means we need more optimizations").
#
# IT NEVER STOPS THE RUN (USER 2026-10-11: "Why does it pause? it shouldn't pause."). A DEFER (rc 4)
# used to `return 1` here -- "a human decides. Stopping before the derivation reads this table" --
# which halted the whole pipeline until someone came back. That is the NEVER BLOCK ON A QUESTION rule
# (CLAUDE.md) in script form: the sign-off is still owed, but it is COLLECTED, not waited for. So the
# run takes the gate's default (the table AS MEASURED -- condemned cells stay reference-only, which the
# phase-D derivation already honours via the `*` marker), records the question in $DECISIONS, and goes
# on. `status` and the COMPLETE line print $DECISIONS until a human deletes it. Prevent Damage
# 2026-10-11 sat stopped at this line over two noise-level pairs (|shift| << SE) that no amount of
# ceiling could settle. A RERUN recommendation (rc 3) is recorded the same way, since nothing
# auto-launches it either.
phase_riskgate() {
    done_p C_riskgate && return 0
    log "PHASE C.5: matrix risk gate"
    python3 scripts/matrix_risk_gate.py "$VLQ" > "$VLQ/riskgate.txt" 2>&1
    local rc=$?
    tee -a "$VLQ/driver.log" < "$VLQ/riskgate.txt"
    case "$rc" in
      0) log "PHASE C.5: CLEAN -- the filter cannot have changed a verdict" ;;
      3|4)
         local what="SIGN-OFF OWED"; [ "$rc" = 3 ] && what="RERUN RECOMMENDED (not auto-launched)"
         log "PHASE C.5: $what -- CONTINUING with the table as measured (the gate's default);"
         log "  the question is recorded in $DECISIONS and shown by \`status\` until a human clears it."
         { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE C.5 risk gate: $what"
           echo "    default TAKEN: the matrix table as measured (condemned cells reference-only)."
           echo "    to act on it instead: see the gate output below; clear this file once decided."
           sed 's/^/    /' "$VLQ/riskgate.txt"; echo; } >> "$DECISIONS" ;;
      *) log "PHASE C.5: gate failed (rc=$rc)"; return 1 ;;
    esac
    mark C_riskgate
}

phase_meta() {
    done_p D_meta && return 0
    local keys; keys=$(staged_keys)
    log "PHASE D: crossover + trust depth ->$keys"
    python3 scripts/attic/valueleaf_table_to_metadata.py "$MATRIX_TXT" \
        --decks $keys --average-seeds 2>&1 | tee -a "$VLQ/driver.log"
    leafrule_counterfactual
    mark D_meta
}

# THE LEAF-RULE COUNTERFACTUAL (USER 2026-10-14: "we should still A/B test the final result with it off
# and on", "separately from the rest of the leaf", "as an additional test if it was adopted"; and on why
# play settings are the right yardstick: "if a setting is fully unreachable due to cost it also will not
# come up so much because the budget will cut it off").
#
# Phase C0 can let a deck's H cells of depth >= K score their horizon with a greedy playout. That never
# touches the model (rows are identical); it can only move the H-dependent SETTINGS derived above -- the
# fallback crossover, value_no_fallback, the trust candidate -- because the greedy H rows may sit higher
# than the 1-ply leaf would have scored them. Playing those cells on the 1-ply leaf to find out is the
# cost C0 exists to avoid, and C0's 50-game screen cannot bound the shift to the derivation's 0.002-turn
# tolerance (that needs ~1,500 pairs per cell). So the OFF product is bounded instead: re-derive with
# each greedy H row shifted DOWN by the most C0's own pairs allow (0 moved in n pairs admits a loss rate
# up to 1 - 0.05^(1/n), one turn per moved game) -- the worst case for the rule, and conservative. If no
# play-relevant setting moves, ON and OFF are the same product and nothing is played. If one does, phase
# E plays the OFF sidecar against the staged one on the A/B seeds -- same model, only those settings
# differ -- and phase E's verdict decides which settings ship.
leafrule_counterfactual() {
    [ -s "$LEAFCAL" ] || return 0
    local row key dir stem mkey base games staged off sh diff
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        staged=logs/eval/$stem.value.STAGED.json
        off=$VLQ/$stem.value.LEAFOFF.json
        rm -f "$off"
        [ -s "$staged" ] || continue
        sh=$(python3 -c '
import json, sys
r = json.load(open(sys.argv[1])).get("decks", {}).get(sys.argv[2])
if r and r.get("K"):
    print(",".join("%s=%.4f" % (d, 1 - 0.05 ** (1.0 / c["paired"]))
                   for d, c in sorted(r["cells"].items(), key=lambda x: int(x[0]))
                   if int(d) >= r["K"] and c["paired"] > 0))' "$LEAFCAL" "${mkey}_staged")
        [ -n "$sh" ] || continue
        log "  leaf rule ($stem): counterfactual H shift $sh (worst case C0's pairs allow)"
        python3 scripts/attic/valueleaf_table_to_metadata.py "$MATRIX_TXT" --decks "${mkey}_staged" \
            --average-seeds --h-shift "$sh" --write-to "$off" >> "$VLQ/driver.log" 2>&1 \
            || { log "  leaf rule ($stem): counterfactual derivation REFUSED (see driver.log) -- no OFF arm"
                 rm -f "$off"
                 { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE D leaf rule: $stem counterfactual REFUSED by the deriver"
                   echo "    the ON settings ship untested against OFF; see $VLQ/driver.log"; echo; } >> "$DECISIONS"
                 continue; }
        diff=$(python3 - "$staged" "$off" <<'PY'
import json, sys
a, b = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
def play(v):
    xo = v.get("value_fallback_crossover") or {}
    return {"crossover": list(zip(xo.get("committed_depths", []), xo.get("take_heuristic_at_hdepth", []))),
            "no_fallback": v.get("value_no_fallback"),
            "trust_candidate": v.get("value_trust_depth_candidate"),
            "escalation_cap": (v.get("value_play") or {}).get("escalation_cap")}
pa, pb = play(a), play(b)
print("; ".join("%s ON=%s OFF=%s" % (k, pa[k], pb[k]) for k in pa if pa[k] != pb[k]))
PY
)
        if [ -z "$diff" ]; then
            log "  leaf rule ($stem): ON and worst-case OFF derive the SAME play settings -- nothing to play"
            rm -f "$off"
            { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE D leaf rule: $stem -- greedy H cells cannot have moved a play setting"
              echo "    worst-case shift $sh changes no crossover / no_fallback / trust candidate / escalation cap"; echo; } >> "$DECISIONS"
        else
            log "  leaf rule ($stem): settings DIFFER under the worst case -- phase E plays staged vs leafOFF"
            log "    $diff"
        fi
    done
}

# ------------- PHASE E0: the EMULATED LADDER's rollout cost per leaf, per deck (value_play.emul_r)
# USER 2026-10-21 ("Let's add this mode to the check in the value-leaf"): phase E also measures the emulated
# ladder -- warm-up passes on the staged model, the committing pass on the rollout leaf at the depth the rollout
# ladder would reach, the model breaking the rollout's ties at the root (MTG_ID_HINT_ROOT=3). It beat the default
# on Landfall at equal instructions (-0.0355 turns) and the shipped shapes on KittyEquipment and Melira Pod in
# the fleet screen (docs/design/leaf-vs-heuristic-asymmetries-2026-10-20.md s7-8). Its replayed start gate needs
# R, the rollout's cost per leaf by pass depth; calibrating R inside each game costs more than it saves on a deck
# with few searched decisions (Landfall 1.11x live), so it is measured ONCE here and frozen into the arm's
# sidecar (scripts/vlq_emul.py). A real data dependency of phase E's emul arm -- R needs the staged model at its
# final play settings, i.e. after D -- so it is its own pooled batch: play-settings games, one short tail.
# Never stops the run: a deck with no model or no usable R simply gets no emul arm, which the log says.
E0_MARK=E0_emulcal
phase_emulcal() {
    done_p "$E0_MARK" && return 0
    local vroot=$VLQ/emulcal_variants; rm -rf "$vroot"; mkdir -p "$vroot"
    local row key dir stem mkey base games staged s r
    { for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        staged=logs/eval/$stem.value.STAGED.json
        rm -f "$VLQ/$stem.emul_r.json"
        [ -s "$staged" ] || continue
        python3 scripts/vlq_emul.py sidecar "$staged" "$vroot/$stem.value.EMULCAL.json" || continue
        make_variant_deck "$vroot/$key" "$dir" "$stem" "$vroot/$stem.value.EMULCAL.json"
        for s in $EMULCAL_SEEDS; do
            h_job "$key-emulcal_s$s" "$(deck_file "$vroot/$key" "$stem")" "$vroot/$key/$stem.profile.json" "$EMULCAL_GAMES" "$s"
        done
      done; } | h_manifest "$VLQ/emulcal.manifest.json" >/dev/null \
        || { log "PHASE E0: no staged model with a learned model -- no emulated arm to calibrate"; mark "$E0_MARK"; return 0; }
    log "PHASE E0: emulated-ladder R calibration, $(grep -c '"name"' "$VLQ/emulcal.manifest.json") jobs in ONE queue"
    MTG_ROLLOUT_STATS=1 "$(harness_bin)" --batch "$VLQ/emulcal.manifest.json" > "$VLQ/emulcal.log" 2> "$VLQ/emulcal.err" \
        || { local rc=$?
             log "PHASE E0: batch FAILED (exit $rc); NOT marked -- re-run to redo it"
             return 1; }
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -d "$vroot/$key" ] || continue
        if r=$(python3 scripts/vlq_emul.py r "$VLQ/emulcal.err" "$vroot/$key/$stem.profile.json" 2>&1); then
            printf '%s\n' "$r" > "$VLQ/$stem.emul_r.json"
            log "  $stem: emul_r $r (frozen into phase E's emul arm)"
        else
            log "  $stem: no usable R -- phase E plays no emul arm ($r)"
        fi
    done
    mark "$E0_MARK"
}

# --------------------- PHASE E: A/B + shape arms + trust for every deck, ONE pooled batch (MEASURED ONLY)
# (No play-setting sweep here any more: it measured d-1/d/d+1 at a FIXED budget and decided nothing. Phase E.7
# measures the depth x budget grid on the leaf that ships and decides -- USER 2026-10-22.)
# Bases are spaced by exactly games-per-job so each arm tiles its seed space once: game identity is
# base+game_index, so closer spacing makes jobs REPLAY games -- which once turned 1.3 sigma into a fake
# -14.4 sigma (rule 7). Job names are "<deck>-<arm>_s<seed>" so the single log splits per deck after.
# Which marker this measurement writes. `run` uses E_measure (the pipeline's phase E, post-matrix).
# `ab` uses E_ab_nomatrix so a later FULL run still executes phase E properly: the two measure
# different staged sidecars (ab: the trained model as-is, presence-only hybrid at built-in default
# play; E: the model with the matrix-derived value_play merged in), so one must not mask the other.
E_MARK=E_measure
phase_measure() {
    done_p "$E_MARK" && return 0
    local vroot=$VLQ/variants; rm -rf "$vroot"; mkdir -p "$vroot"
    local row key dir stem mkey base games staged s d
    { for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        staged=logs/eval/$stem.value.STAGED.json
        [ -s "$staged" ] || continue
        make_variant_deck "$vroot/$key/live"   "$dir" "$stem" "$dir/$stem.value.json"
        make_variant_deck "$vroot/$key/staged" "$dir" "$stem" "$staged"
        for s in $AB_SEEDS; do
            h_job "$key-live_s$s"   "$(deck_file "$vroot/$key/live" "$stem")"   "$vroot/$key/live/$stem.profile.json"   "$AB_GAMES" "$s"
            h_job "$key-staged_s$s" "$(deck_file "$vroot/$key/staged" "$stem")" "$vroot/$key/staged/$stem.profile.json" "$AB_GAMES" "$s"
        done
        # SEARCH-SHAPE ARMS (USER 2026-10-14: "Do we have a no-leaf screen in the pipeline anywhere? I think
        # we probably should."). The model is only worth its cost if it beats the LEAFLESS shapes on the
        # menu (docs/design/per-deck-search-shape.md), so they are measured beside it:
        #   nlesc = #2, escalation with a leafless probe    {ladder: escalation, leaf: none, alpha: relaxed}
        #   nlfit = #3, the final-depth single pass         {ladder: single,     leaf: none, alpha: relaxed}
        # Built on the SIDECAR ROUTE -- the live sidecar plus the shape keys, i.e. exactly the file E.5 would
        # install -- never the value_profile "noleaf" stand-in, which discards a tuned value_play block and
        # inverted Creature Giving's verdict (that doc's finding 12). Same seeds and games as live/staged, so
        # every arm is paired with both. A shape the live sidecar already ships is skipped: it IS the live arm.
        # Measured at the deck's PLAY settings only, like the leaf itself; the shape doc's stricter rule (no
        # worse at every configuration the deck is played at) is not applied here.
        for sh in nlesc nlfit; do
            rm -f "$VLQ/$stem.value.${sh^^}.json"
            python3 - "$dir/$stem.value.json" "$VLQ/$stem.value.${sh^^}.json" "$sh" <<'PY' || continue
import json, os, sys
live, out, sh = sys.argv[1:4]
v = json.load(open(live)) if os.path.exists(live) and os.path.getsize(live) else {}
want = {"nlesc": ("escalation", "none"), "nlfit": ("single", "none")}[sh]
vp = v.get("value_play") or {}
if ((vp.get("ladder") or "escalation"), (vp.get("leaf") or "model")) == want and vp.get("alpha") == "relaxed":
    sys.exit(1)
vp = v.setdefault("value_play", {})
vp["ladder"], vp["leaf"], vp["alpha"] = want[0], want[1], "relaxed"
json.dump(v, open(out, "w"), indent=1)
PY
            make_variant_deck "$vroot/$key/$sh" "$dir" "$stem" "$VLQ/$stem.value.${sh^^}.json"
            for s in $AB_SEEDS; do
                h_job "$key-${sh}_s$s" "$(deck_file "$vroot/$key/$sh" "$stem")" "$vroot/$key/$sh/$stem.profile.json" "$AB_GAMES" "$s"
            done
        done
        # EMULATED-LADDER ARM (USER 2026-10-21; phase E0 above): the STAGED model in the emulated ladder with the
        # frozen R phase E0 measured -- exactly the file E.5 would install. Built on the staged sidecar, not the
        # live one: the warm-ups run on the model, so it is this run's model being judged. Measured even when live
        # already ships the emulated ladder (live runs the old model and the old R).
        rm -f "$VLQ/$stem.value.EMUL.json"
        if [ -s "$VLQ/$stem.emul_r.json" ] \
           && python3 scripts/vlq_emul.py sidecar "$staged" "$VLQ/$stem.value.EMUL.json" --r "$VLQ/$stem.emul_r.json"; then
            make_variant_deck "$vroot/$key/emul" "$dir" "$stem" "$VLQ/$stem.value.EMUL.json"
            for s in $AB_SEEDS; do
                h_job "$key-emul_s$s" "$(deck_file "$vroot/$key/emul" "$stem")" "$vroot/$key/emul/$stem.profile.json" "$AB_GAMES" "$s"
            done
        fi
        # LEAF-RULE A/B (see leafrule_counterfactual): the worst-case OFF settings on the A/B seeds, paired
        # against the staged arm above, which IS the ON product -- so it costs one arm, not two.
        if [ -s "$VLQ/$stem.value.LEAFOFF.json" ]; then
            make_variant_deck "$vroot/$key/leafoff" "$dir" "$stem" "$VLQ/$stem.value.LEAFOFF.json"
            for s in $AB_SEEDS; do
                h_job "$key-leafOFF_s$s" "$(deck_file "$vroot/$key/leafoff" "$stem")" "$vroot/$key/leafoff/$stem.profile.json" "$AB_GAMES" "$s"
            done
        fi
        # TRUST ACCEPTANCE arms, pooled into this same queue (never a second batch -- a wave is a
        # loop). Only when the matrix nominated a candidate; the OFF arm is the staged model exactly
        # as it stands (no trust), the ON arm is that plus value_trust_depth = the candidate, so the
        # single difference between them is the lever under test.
        _cand=$(python3 -c "
import json,sys
try:    print(json.load(open('$staged')).get('value_trust_depth_candidate') or '')
except Exception: print('')")
        if [ -n "$_cand" ]; then
            python3 - "$staged" "$vroot/$key/trust_on.value.json" "$_cand" <<'PY'
import json, sys
v = json.load(open(sys.argv[1]))
v["value_trust_depth"] = int(sys.argv[3])
json.dump(v, open(sys.argv[2], "w"), indent=1)
PY
            make_variant_deck "$vroot/$key/ptrust_on"  "$dir" "$stem" "$vroot/$key/trust_on.value.json"
            make_variant_deck "$vroot/$key/ptrust_off" "$dir" "$stem" "$staged"
            for s in $TRUST_SEEDS; do
                h_job "$key-trustON_s$s"  "$(deck_file "$vroot/$key/ptrust_on" "$stem")"  "$vroot/$key/ptrust_on/$stem.profile.json"  "$TRUST_GAMES" "$s"
                h_job "$key-trustOFF_s$s" "$(deck_file "$vroot/$key/ptrust_off" "$stem")" "$vroot/$key/ptrust_off/$stem.profile.json" "$TRUST_GAMES" "$s"
            done
        fi
      done; } | h_manifest "$VLQ/measure.manifest.json" >/dev/null
    log "PHASE E: $(grep -c '"name"' "$VLQ/measure.manifest.json") jobs (A/B + shape + trust arms, every deck) in ONE queue"
    # Exit status gates the phase, same as phase A (2026-09-05 OOM lesson): a killed batch must not
    # let truncated measurements read as an A/B verdict.
    "$(harness_bin)" --batch "$VLQ/measure.manifest.json" > "$VLQ/measure.log" 2> "$VLQ/measure.err" \
        || { local rc=$?
             log "PHASE E: batch FAILED (exit $rc -- 137=SIGKILL, think OOM); NOT interpreting the"
             log "  partial measure.log. Re-run to redo the phase."
             return 1; }
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        grep "^$key-" "$VLQ/measure.log" > "$VLQ/m_$key.log" 2>/dev/null
        [ -s "$VLQ/m_$key.log" ] || continue
        sed -i "s/^$key-//" "$VLQ/m_$key.log"
        echo "=== $key: regenerated value-leaf vs live ===" | tee -a "$VLQ/driver.log"
        grep -E "^(live|staged)_s" "$VLQ/m_$key.log" > "$VLQ/m_${key}_ab.log"
        python3 scripts/vlq_ab_report.py "$VLQ/m_${key}_ab.log" live 2>&1 | tee -a "$VLQ/driver.log"
        if grep -qE "^(nlesc|nlfit|emul)_s" "$VLQ/m_$key.log"; then
            echo "=== $key: search shapes -- the staged model, the leafless menu shapes and the emulated ladder, all vs live ===" | tee -a "$VLQ/driver.log"
            grep -E "^(live|staged|nlesc|nlfit|emul)_s" "$VLQ/m_$key.log" > "$VLQ/m_${key}_shape.log"
            python3 scripts/vlq_ab_report.py "$VLQ/m_${key}_shape.log" live 2>&1 | tee -a "$VLQ/driver.log"
        else
            rm -f "$VLQ/m_${key}_shape.log"
        fi
        # LEAF-RULE VERDICT. ON (staged) vs worst-case OFF at play settings. ON keeps its settings unless it
        # is NOTABLY WORSE than OFF (delta > $TRUST_TOL turns at paired t > +2, the adoption bar's quality
        # test); then OFF's H-dependent settings replace ON's in the staged model -- they lean toward
        # falling back to the heuristic, the safe side. The adoption A/B above measured ON, which the
        # record says. Cost is not judged: the two differ only in which line is taken, not in search.
        if grep -qE "^leafOFF_s" "$VLQ/m_$key.log"; then
            echo "=== $key: leaf rule -- staged (greedy H cells, ON) vs worst-case 1-ply settings (OFF) ===" | tee -a "$VLQ/driver.log"
            grep -E "^(staged|leafOFF)_s" "$VLQ/m_$key.log" > "$VLQ/m_${key}_leafrule.log"
            local lv; lv=$(python3 scripts/vlq_ab_report.py "$VLQ/m_${key}_leafrule.log" leafOFF --verdict staged \
                             --tol "$TRUST_TOL" 2>&1 | tee -a "$VLQ/driver.log" | grep '^VERDICT')
            if python3 - "$lv" "$TRUST_TOL" <<'PY'
import re, sys
m = re.search(r"delta=([-+\d.]+) t=([-+\d.]+)", sys.argv[1])
sys.exit(0 if m and float(m.group(1)) > float(sys.argv[2]) and float(m.group(2)) > 2.0 else 1)
PY
            then
                python3 - "logs/eval/$stem.value.STAGED.json" "$VLQ/$stem.value.LEAFOFF.json" <<'PY'
import json, sys
st, off = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
for k in ("value_leaf_table", "value_fallback_crossover", "value_no_fallback"):
    if k in off: st[k] = off[k]
    else: st.pop(k, None)
json.dump(st, open(sys.argv[1], "w"))
PY
                log "  leaf rule ($stem): ON NOTABLY WORSE than worst-case OFF -- OFF's settings installed in the staged model"
                { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE E leaf rule: $stem -- ON notably worse, OFF settings SHIPPED in staged"
                  echo "    $lv"
                  echo "    note: the staged-vs-live adoption A/B measured the ON settings"; echo; } >> "$DECISIONS"
            else
                log "  leaf rule ($stem): ON not notably worse than worst-case OFF -- ON settings kept"
                { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE E leaf rule: $stem -- ON kept (not notably worse than worst-case OFF)"
                  echo "    $lv"; echo; } >> "$DECISIONS"
            fi
        fi
        # TRUST ACCEPTANCE. The matrix nominated the candidate; these games decide it. --apply
        # promotes an accepted candidate into the STAGED model's value_trust_depth, which is not
        # adoption by itself -- the staged model goes live only through phase E.5 below.
        if grep -qE "^trust(ON|OFF)_s" "$VLQ/m_$key.log"; then
            echo "=== $key: trust acceptance (ON vs OFF, held-out seeds) ===" | tee -a "$VLQ/driver.log"
            grep -E "^trust(ON|OFF)_s" "$VLQ/m_$key.log" > "$VLQ/m_${key}_trust.log"
            python3 scripts/vlq_trust_accept.py "$VLQ/m_${key}_trust.log" \
                "logs/eval/$stem.value.STAGED.json" --tol "$TRUST_TOL" --apply 2>&1 | tee -a "$VLQ/driver.log"
        fi
    done
    mark "$E_MARK"
}

# ------------- PHASE E.5: ADOPT a better leaf, automatically, without stopping
# USER 2026-10-11: "We should adopt automatically if the leaf is better. Nothing prevents you from
# updating or asking the user as long as you do not stop." A deck's FIRST model used to end the run at
# F (which needs the leaf live) until somebody came back to install it -- a stop by another name. Now,
# per deck, phase E's A/B decides (vlq_ab_report.py --verdict). USER: "Is it better is primarily a
# performance metric, but we may need to wait for a user response if the quality is notably worse" --
# "that just means we do not adopt and continue with any other work that remains". So: ADOPT iff the
# staged leaf is CHEAPER than live (core-s ratio < 1) and its quality is NOT NOTABLY WORSE (not: delta
# > $TRUST_TOL turns at paired t > +2). Adopted -> installed as <dir>/<stem>.value.json in THIS
# checkout and F runs on it; a replaced live model is kept as $VLQ/<stem>.value.PREV.json. Not adopted
# (dearer -- even if better, which is a TRADE for the user -- or notably worse) -> the live state is
# left exactly as it was and the run carries on; a first model stays staged and F skips it. Either way
# the verdict line, with its reason and cost ratio, goes to $DECISIONS. This is adoption IN THIS TREE: nothing is committed;
# committing it with the deck's smoke+regression GT re-accepted (the leaf changes play) is the next step.
# THE CANDIDATES (USER 2026-10-14, the no-leaf screen): not only the staged model but also phase E's leafless
# menu shapes (nlesc, nlfit) and the emulated ladder (emul, USER 2026-10-21), each judged against live by the same rule; scripts/vlq_adopt_pick.py picks among
# the ones that pass (the cheapest, unless another is notably better than it) and says why.
phase_adopt() {
    done_p E_adopt && return 0
    log "PHASE E.5: adopt a better leaf (phase E A/B verdict)"
    local row key dir stem mkey base games staged v rc alog pick src what
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        staged="logs/eval/$stem.value.STAGED.json"
        [ -s "$staged" ] || continue
        # The shape log (live, staged and the leafless menu shapes) when phase E measured shapes; otherwise
        # the plain live-vs-staged log, so a queue measured before the shape arms existed still decides.
        alog="$VLQ/m_${key}_shape.log"; [ -s "$alog" ] || alog="$VLQ/m_${key}_ab.log"
        [ -s "$alog" ] || { log "  $stem: no A/B log -- left as is"; continue; }
        v=$(python3 scripts/vlq_adopt_pick.py "$alog" --tol "$TRUST_TOL"; exit $?)
        rc=$?
        pick=$(printf '%s\n' "$v" | sed -n 's/^PICK arm=\([^ ]*\).*/\1/p')
        case "$pick" in
          staged) src=$staged;                         what="the staged value leaf" ;;
          nlesc)  src=$VLQ/$stem.value.NLESC.json;     what="shape #2, escalation with a leafless probe" ;;
          nlfit)  src=$VLQ/$stem.value.NLFIT.json;     what="shape #3, the final-depth single pass (leafless)" ;;
          emul)   src=$VLQ/$stem.value.EMUL.json;      what="the emulated ladder (staged model warm-ups, rollout committing pass, frozen emul_r)" ;;
          *)      src=""; what="" ;;
        esac
        case "$rc" in
          0) [ -s "$src" ] || { log "  $stem: picked $pick but $src is missing -- install FAILED"; return 1; }
             [ -e "$dir/$stem.value.json" ] && cp -f "$dir/$stem.value.json" "$VLQ/$stem.value.PREV.json"
             cp -f "$src" "$dir/$stem.value.json" || { log "  $stem: install FAILED"; return 1; }
             log "  ADOPTED $stem: $what -> $dir/$stem.value.json (this tree, NOT committed)"
             printf '%s\n' "$v" | while IFS= read -r l; do log "    $l"; done
             { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE E.5: ADOPTED $stem -- $what (installed in this tree, NOT committed)"
               printf '%s\n' "$v" | sed 's/^/    /'
               [ "$pick" != staged ] && echo "    the staged model was NOT installed; it stays at $staged"
               [ -e "$VLQ/$stem.value.PREV.json" ] && echo "    previous live sidecar kept as $VLQ/$stem.value.PREV.json"
               echo "    next: commit it with the deck's smoke+regression GT re-accepted (it changes play)."
               echo; } >> "$DECISIONS" ;;
          2) log "  NOT adopted $stem -- live state left as is; CONTINUING (the reason is the user's to rule on)"
             printf '%s\n' "$v" | while IFS= read -r l; do log "    $l"; done
             { echo "=== $(date '+%Y-%m-%dT%H:%M:%S')  PHASE E.5: NOT ADOPTED $stem -- USER's call (reasons in the VERDICTs)"
               printf '%s\n' "$v" | sed 's/^/    /'
               echo "    the staged model stays at $staged; nothing in $dir changed. To adopt a candidate anyway:"
               echo "    cp '$staged' '$dir/$stem.value.json' (or $VLQ/$stem.value.NLESC.json / .NLFIT.json for a"
               echo "    leafless shape, .EMUL.json for the emulated ladder), delete the E6_skipcal and E7_playset markers, and re-run."
               echo; } >> "$DECISIONS" ;;
          *) log "  $stem: verdict FAILED (rc=$rc) -- see $alog"; printf '%s\n' "$v" | while IFS= read -r l; do log "    $l"; done
             return 1 ;;
        esac
    done
    mark E_adopt
}

# ------------- PHASE E.6: the deck's ESCALATION-SKIP ALLOWANCE (sidecar escalation_skip_r)
# USER 2026-10-13: "Let's build the calibration stage. I would like to make it relatively fast for most decks.
# So it would give up if there is not a notable amount of headroom. So maybe 4%+." All of it is in
# scripts/esc_skip_stage.py (three pooled batches over every deck: a 1200-game R = 0 probe that gives up below
# 4% headroom, a held-out paired A/B, and a one-game isolation replay of every moved game; adopt only with no
# real slower game). Design and evidence: docs/design/escalation-skip-calibration-stage.md.
#   * AFTER E.5, because the allowance is a measurement OF the probe -- the leaf now live. A regeneration used to
#     carry the old key over unchanged, fitted to the previous leaf.
#   * BEFORE F, because it can change play, and F (and every mulligan generation after it) is fitted to play.
# Like E.5 it installs into the live sidecar IN THIS TREE, commits nothing, never stops to ask, and records
# every outcome in DECISIONS.txt. A deck that already has a key gets it as a third A/B arm and the bar picks
# (both pass: the bigger saving; neither: the key is removed). A deck that gives up keeps its key, flagged.
phase_skipcal() {
    done_p E6_skipcal && return 0
    log "PHASE E.6: escalation-skip allowance (probe -> 4% headroom gate -> A/B -> isolation)"
    local row key dir stem mkey base games skipped="" dirs=()
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        # Same deck set as F: decks this run staged a model for, and only once a leaf is live.
        [ -s "logs/eval/$stem.value.STAGED.json" ] || continue
        if [ ! -e "$dir/$stem.value.json" ]; then
            log "  PHASE E.6: $stem has no live leaf (not adopted, see $DECISIONS) -- skipped; re-run after installing"
            skipped="$skipped $stem"; continue
        fi
        dirs+=("$dir")
    done
    if [ ${#dirs[@]} -gt 0 ]; then
        # PIPESTATUS[0], not $? (that is tee's) -- the phase-F lesson.
        python3 scripts/esc_skip_stage.py "${dirs[@]}" --work "$VLQ/skipcal" --decisions "$DECISIONS" --write 2>&1 \
            | tee -a "$VLQ/driver.log"
        [ "${PIPESTATUS[0]}" -eq 0 ] || { log "  PHASE E.6: FAILED -- NOT marked done; a re-run reuses every finished batch"; return 1; }
    fi
    [ -n "$skipped" ] && { log "  PHASE E.6: NOT marked done (skipped:$skipped) -- nothing is pending on it in this run"; return 0; }
    mark E6_skipcal
}

# ------------- PHASE E.7: the deck's PLAY SETTING (the live sidecar's enabled value_play)
# USER 2026-10-22: "The leaf should have a stage that decides." The documented route to an enabled block was a
# manual recommend -> accept exercise that nothing performed any more, so most leaves shipped with none and played
# the built-in d5 / 20 undecided. All of it is in scripts/play_setting_stage.py: ONE pooled batch per run, the live
# sidecar against an enabled depth {d-1,d,d+1} x budget {b/2,b,2b} grid -- and the same grid for every phase-E
# shape still in the running (passed E.5 but not installed, or dearer but notably better: a budget is part of a
# shape) -- 8 held-out seeds x 1000 paired games;
# adopted by the E.5 rule (vlq_adopt_pick.py: the cheapest arm not notably worse; a dearer-but-better arm is a
# TRADE recorded for the user), installed in the live sidecar IN THIS TREE, every outcome in DECISIONS.txt.
#   * LAST, after E.5 and E.6: it measures the leaf and skip allowance that ship, and the setting it picks changes
#     play, which the deck's GT (and any later mulligan generation) must then be fitted to.
phase_playset() {
    done_p E7_playset && return 0
    log "PHASE E.7: play setting (depth x budget grid on the live leaf -> the E.5 adoption rule)"
    local row key dir stem mkey base games skipped="" dirs=() shapes=()
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -s "logs/eval/$stem.value.STAGED.json" ] || continue
        if [ ! -e "$dir/$stem.value.json" ]; then
            log "  PHASE E.7: $stem has no live leaf (not adopted, see $DECISIONS) -- skipped; re-run after installing"
            skipped="$skipped $stem"; continue
        fi
        dirs+=("$dir")
        # Phase E judged the shapes at ONE budget; the ones still in the running get the same grid (USER
        # 2026-10-22, play_setting_stage.py "THE OTHER PHASE-E SHAPES").
        [ -s "$VLQ/m_${key}_shape.log" ] && shapes+=(--phase-e "$key=$VLQ/m_${key}_shape.log")
    done
    if [ ${#dirs[@]} -gt 0 ]; then
        python3 scripts/play_setting_stage.py "${dirs[@]}" "${shapes[@]}" --work "$VLQ/playset" --decisions "$DECISIONS" --write 2>&1 \
            | tee -a "$VLQ/driver.log"
        [ "${PIPESTATUS[0]}" -eq 0 ] || { log "  PHASE E.7: FAILED -- NOT marked done; a re-run reuses a finished batch"; return 1; }
    fi
    [ -n "$skipped" ] && { log "  PHASE E.7: NOT marked done (skipped:$skipped) -- nothing is pending on it in this run"; return 0; }
    mark E7_playset
}

# ------------- (PHASE F, the mulligan-generation setting screen, MOVED to scripts/mullgen.sh run, USER
# 2026-10-18: "remove it as a value-leaf stage and make it a mulligan profile stage that only runs when it
# hasn't been done yet." The screen runs before a deck's first keep-table generation, value leaf or not.)

case "${1:-status}" in
finish|run)
    # `finish` is the escape hatch for the one downside of pooling: phase A is all-or-nothing, so
    # running out of time INSIDE it would otherwise leave rows but no models, tables or measurements
    # -- whereas per-deck staging banks finished decks as it goes. Rows are flushed per row and so are
    # durable at every instant, and every later phase works fine on fewer rows. So `finish` accepts the
    # rows on disk as final and runs B..E on them. Use it when the clock, not the data, is the limit.
    if [ "$1" = finish ]; then
        n=$(grep -vc '^#' "$ALL_ROWS" 2>/dev/null || echo 0)
        [ "$n" -gt 0 ] || { echo "no rows at $ALL_ROWS -- nothing to finish"; exit 1; }
        if pgrep -f "valueleaf_regen_queue.sh run" >/dev/null 2>&1; then
            echo "a 'run' is still going -- stop it first, or its batch will fight this one for the box"
            exit 1
        fi
        log "FINISH: accepting $n rows as final and running phases B..E on them"
        mark A_rows
    fi
    # THE ENGINE: PGO+LTO (user, 2026-10-08). Build it from HEAD:src first if it is not; every phase's
    # games then resolve through harness_bin (and scripts/engine_bin.py for the matrix), which uses
    # build/PGO only while it matches HEAD:src exactly -- byte-identical to Release, ~1.3x faster.
    harness_pgo_ensure
    log "=== value-leaf + play-profile regeneration: START (3 pooled phases, 3 tails) ==="
    _eb=$(harness_bin) && log "engine: $_eb ($(harness_bin_kind "$_eb"); MTG_PGO=0 pins Release)"
    for ph in phase_freeze phase_rows phase_split phase_train phase_leafcal phase_matrix phase_riskgate \
              phase_meta phase_emulcal phase_measure phase_adopt phase_skipcal phase_playset; do
        check_freeze || exit 1
        check_decks  || exit 1
        check_leaf_keys || exit 1
        $ph || { log "STOPPED at $ph"; exit 1; }
    done
    log "=== COMPLETE -- review the A/B + sweep reports above ==="
    [ -s "$DECISIONS" ] && log "=== DECISIONS taken without stopping (adoptions, defaults on owed sign-offs): $DECISIONS ==="
    ;;
status)
    # A real progress report: where the run is, what is left, and how to resume. Reads only files the
    # phases already write, so it is safe to run against a live run.
    ph_label() { case $1 in
        00_freeze) echo "0 freeze+build";; A_rows)   echo "A rows        ";;
        A_split)   echo "A split       ";; B_train)  echo "B train       ";;
        C0_leafcal) echo "C0 leaf calib ";;
        C_matrix)  echo "C matrix      ";; C_riskgate) echo "C.5 risk gate ";;
        D_meta)    echo "D metadata    ";; E0_emulcal) echo "E0 emul R     ";;
        E_measure)  echo "E measure     ";;
        E_adopt)   echo "E.5 adopt     ";;
        E6_skipcal) echo "E.6 skip allow";;
        E7_playset) echo "E.7 play set  ";; esac; }
    echo "queue dir  : $VLQ"
    echo "frozen at  : $(cat "$VLQ/freeze.commit" 2>/dev/null || echo '(not started)')"
    now=$(src_fingerprint); frz=$(cat "$VLQ/freeze.src" 2>/dev/null || echo "")
    if [ -n "$frz" ] && [ "$now" != "$frz" ]; then
        # src moved. Whether that MATTERS is a play question, and `run` answers it by re-folding the
        # smoke digests (see check_freeze). status must not run the suite -- it is documented as
        # touching nothing -- so it reports what is pending rather than pre-judging it.
        echo "freeze     : src/ moved -- \`run\` re-checks the PLAY digests and CONTINUES either way:"
        echo "             unchanged (stamped $(cut -c1-12 "$VLQ/freeze.play" 2>/dev/null || echo 'none')) keeps every game;"
        echo "             changed keeps the completed full sets and re-runs the rest."
    elif [ -n "$frz" ]; then
        echo "freeze     : intact ($(cut -c1-12 "$VLQ/freeze.src"))"
    fi
    echo
    echo "phases:"
    for p in 00_freeze A_rows A_split B_train C0_leafcal C_matrix C_riskgate D_meta E0_emulcal E_measure E_adopt E6_skipcal E7_playset; do
        if done_p "$p"; then printf "  [x] %s  %s\n" "$(ph_label "$p")" "$(cat "$DONE/$p")"
        else                 printf "  [ ] %s\n" "$(ph_label "$p")"; fi
    done
    if [ -s "$LEAFCAL" ]; then
        echo "leaf calib : $(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))['decks']
print('; '.join('%s K=%s' % (k, v['K'] or 'none') for k, v in sorted(d.items())))" "$LEAFCAL" 2>/dev/null)"
    fi
    echo
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        r=$(grep -vc '^#' "$ROWDIR/$stem.rows" 2>/dev/null || echo 0)
        if [ "$r" = 0 ]; then
            r=$(awk -v b="$(( base / 100000 ))" '!/^#/ && int($(NF-1)/100000)==b' "$ALL_ROWS" 2>/dev/null | wc -l)
        fi
        if [ -e "$dir/$stem.value.json" ]; then sc="live sidecar=yes (regeneration)"
        else                                    sc="live sidecar=no (first model)"; fi
        printf "  %-14s %s labelled rows (from %s games)   staged=%-4s %s\n" \
            "$key" "$r" "$games" \
            "$( [ -s "logs/eval/$stem.value.STAGED.json" ] && echo yes || echo no )" "$sc"
        rmse=$(grep -oE 'heldout_RMSE=[0-9.]+' "$VLQ/train_$key.log" 2>/dev/null | tail -1)
        [ -n "$rmse" ] && printf "  %-14s %s\n" "" "$rmse"
    done
    echo
    echo "pooled rows: $(grep -vc '^#' "$ALL_ROWS" 2>/dev/null || echo 0)"
    # Matrix progress comes from the CELLS file, not from matrix.log. The log is append-only across
    # resumes, so its last line is whatever the PREVIOUS run said -- it reported "9 cells intractable"
    # for half an hour after a resume that had made condemnation impossible. The cells file is the
    # live state the scheduler itself reads.
    if [ -s "$MATRIX_TXT.cells.json" ]; then
        python3 - "$MATRIX_TXT.cells.json" "$MATRIX_TARGET" <<'PY'
import json, os, sys, collections
cells = json.load(open(sys.argv[1])); target = int(sys.argv[2])
done = sum(1 for c in cells if (c.get("games") or 0) >= target)
cond = sum(1 for c in cells if c.get("intractable"))
have = sum(c.get("games") or 0 for c in cells); want = target * len(cells)
print("matrix     : %d/%d cells at %d games, %d condemned  (%d/%d games, %.0f%%)"
      % (done, len(cells), target, cond, have, want, 100.0 * have / max(want, 1)))
# A cell with 0 games has no rate of its own; borrowing its depth's mean keeps it in the estimate
# instead of costing 0, which is how "123 core-h left" hid a 150 core-h job.
rate = {}
for c in cells:
    if c.get("games"):
        rate.setdefault((c["arm"], c["depth"]), []).append(c["ms"] / c["games"])
short = collections.defaultdict(list)
total = 0.0
for c in sorted(cells, key=lambda c: (c["arm"], c["depth"], c["seed"])):
    g = c.get("games") or 0
    if g >= target:
        continue
    peers = rate.get((c["arm"], c["depth"]), [])
    spg = c["ms"] / g if g else (sum(peers) / len(peers) if peers else 0.0)
    total += (target - g) * spg
    short["%s%d" % (c["arm"], c["depth"])].append((c["seed"], g, spg))
for k, v in short.items():
    left = sum((target - g) * s for _, g, s in v) / 3600.0
    print("             %-3s short on %d seed(s): %s   ~%.1f core-h left"
          % (k, len(v), " ".join("%s:%dg" % (s, g) for s, g, _ in v), left))
if total:
    # Cores the process may actually use, matching the engine's AffinityCpuCount() -- not a hardcoded
    # 20, which is what made a 275-core-hour job look like it might fit an evening. This ETA is the
    # multiplication that was skipped before the 2026-08-10 run: sizing the job first would have shown
    # a ~14 h floor and made 23 h at 5% complete obviously a scheduling failure, not an engine one.
    cores = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 8)
    print("             ~%.0f core-h remaining -> ~%.1f h wall at %d cores (FULL packing; if the"
          " heartbeat shows fewer busy, that is the problem)"
          % (total / 3600.0, total / 3600.0 / cores, cores))
PY
    fi
    [ -s "$MATRIX_TXT" ] && echo "             table -> $MATRIX_TXT"
    # `pgrep -c` PRINTS 0 and EXITS 1 on no match, so the old `|| echo 0` appended a SECOND zero:
    # $live became the two-line string "0\n0", which `!= 0` reports as RUNNING. Every idle queue read
    # "a games phase is active", which is the one thing this line exists to tell you apart. Count the
    # lines instead -- one number, always. `-x` (exact comm) is deliberate: `-f` self-matches this
    # very script's command line. mtg.run is the regression harness's copy of the same binary; it
    # competes for the box just as hard, so it counts.
    live=$(pgrep -x 'mtg|mtg\.run' 2>/dev/null | wc -l)
    if [ "$live" -gt 0 ]; then echo "running    : yes ($live mtg) -- a games phase is active"
    else                       echo "running    : no"; fi
    echo
    echo "resume     : bash $0 run${DECK_ARGS:-}"
    echo "             Re-running is SAFE and incremental: finished phases are skipped via marker"
    echo "             files in $DONE, and rows dedupe on (seed,turn), so an interrupted dump loses"
    echo "             only in-flight games. To redo one phase, delete its marker and re-run."
    echo "             If the CLOCK (not the data) ran out inside phase A, use 'finish' instead:"
    echo "             it accepts the rows on disk as final and runs B..E on them."
    if [ -s "$DECISIONS" ]; then
        echo
        echo "DECISIONS taken without stopping ($DECISIONS) -- delete the file once read:"
        grep -E '^=== |default TAKEN|VERDICT' "$DECISIONS" | sed 's/^/  /'
    fi
    tail -5 "$VLQ/driver.log" 2>/dev/null
    exit 0
    ;;
ab)
    # ADOPTION A/B WITHOUT THE MATRIX. Phase E is matrix-independent in code (nothing in
    # phase_measure reads the table; a staged model with no value_play block is measured as the
    # presence-only hybrid at built-in default play -- exactly what shipping it now would mean).
    # This mode exists for the deck whose keep-table generation NEEDS the leaf as a cost lever
    # before the matrix is worth running (Mirrorwing, 2026-08-14): settle sidecar adoption first,
    # generate the keep table under the settled configuration, run the matrix later against final
    # play. Own marker (E_ab_nomatrix), so a later full `run` still executes its post-matrix E.
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -s "logs/eval/$stem.value.STAGED.json" ] || {
            echo "no staged model for $key (logs/eval/$stem.value.STAGED.json) -- run phases A..B first"; exit 2; }
    done
    E_MARK=E_ab_nomatrix
    E0_MARK=E0_emulcal_nomatrix   # R at pre-matrix play settings; a later full run recalibrates under its own marker
    log "=== NO-MATRIX ADOPTION A/B: staged (as-is, presence-only) vs live ==="
    check_freeze || exit 1
    check_decks  || exit 1
    check_leaf_keys || exit 1
    phase_freeze && phase_emulcal && phase_measure || { log "STOPPED in ab"; exit 1; }
    log "=== AB COMPLETE -- nothing adopted; review the report above ==="
    ;;
skipcal)
    # PHASE E.6 ALONE, for decks whose leaf is ALREADY live: the one-time backfill of the fleet, or a leaf
    # installed by hand. No marker -- it always re-decides -- and no freeze check: it measures the tree as it
    # stands and fits nothing that a later src change could strand. Named decks, else every REGRESSION-SUITE
    # deck with a live value sidecar: an archived predecessor list (decks/<Stem>/v<N>-<slug>/) keeps its sidecar
    # but is no longer played, and has no GT to re-accept an adoption against. Pooled like every other phase:
    # three batches in total, not three per deck.
    harness_pgo_ensure
    _dirs=()
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -e "$dir/$stem.value.json" ] || continue
        if [ ${#DECK_DIRS[@]} -eq 0 ] && ! python3 scripts/suite_gate.py --require "$dir" > /dev/null 2>&1; then
            log "  skipcal: $dir is not a regression-suite deck (an archived list?) -- skipped"; continue
        fi
        _dirs+=("$dir")
    done
    [ ${#_dirs[@]} -gt 0 ] || { echo "no deck with a live value sidecar to calibrate"; exit 2; }
    log "=== PHASE E.6 ALONE (skipcal): ${#_dirs[@]} deck(s) ==="
    python3 scripts/esc_skip_stage.py "${_dirs[@]}" --work "$VLQ/skipcal" --decisions "$DECISIONS" --write 2>&1 \
        | tee -a "$VLQ/driver.log"
    _rc=${PIPESTATUS[0]}
    [ -s "$DECISIONS" ] && log "=== DECISIONS taken without stopping: $DECISIONS ==="
    exit "$_rc"
    ;;
playset)
    # PHASE E.7 ALONE, for decks whose leaf is ALREADY live: the backfill of the leaves that shipped with no decided
    # play setting, or a leaf installed by hand. No marker -- it always re-decides -- and no freeze check: it
    # measures the tree as it stands. Named decks, else every REGRESSION-SUITE deck with a live value sidecar (an
    # archived list has no GT to re-accept an adoption against). ONE pooled batch over all of them.
    harness_pgo_ensure
    _dirs=()
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -e "$dir/$stem.value.json" ] || continue
        if [ ${#DECK_DIRS[@]} -eq 0 ] && ! python3 scripts/suite_gate.py --require "$dir" > /dev/null 2>&1; then
            log "  playset: $dir is not a regression-suite deck (an archived list?) -- skipped"; continue
        fi
        _dirs+=("$dir")
    done
    [ ${#_dirs[@]} -gt 0 ] || { echo "no deck with a live value sidecar to decide"; exit 2; }
    log "=== PHASE E.7 ALONE (playset): ${#_dirs[@]} deck(s) ==="
    python3 scripts/play_setting_stage.py "${_dirs[@]}" --work "$VLQ/playset" --decisions "$DECISIONS" --write 2>&1 \
        | tee -a "$VLQ/driver.log"
    _rc=${PIPESTATUS[0]}
    [ -s "$DECISIONS" ] && log "=== DECISIONS taken without stopping: $DECISIONS ==="
    exit "$_rc"
    ;;
*) echo "usage: $0 {run|ab|skipcal|playset|status} [deck-dir ...]"; echo "  $0 run                          # regenerate the whole fleet"; echo "  $0 run decks/FiveColour         # one deck, new or existing"; echo "  $0 run decks/A decks/B          # two+ decks in ONE pooled queue (3 tails, not 3 per deck)"; echo "  $0 ab  decks/FiveColour         # adoption A/B of the staged model, no matrix needed"; echo "  $0 skipcal [decks/X ...]        # phase E.6 alone: escalation-skip allowance for live leaves"; echo "  $0 playset [decks/X ...]        # phase E.7 alone: decide the play setting for live leaves"; echo "  the deck(s) are the ONLY input; NEVER_CONDEMN (floor 5) is the one setting"; exit 2 ;;
esac
