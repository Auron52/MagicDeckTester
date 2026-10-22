#pragma once

#include <atomic>
#include <chrono>
#include "../core/EnvFlags.h"

// Per-GAME work ceiling: a deterministic way to ABANDON a game whose cost explodes, rather than
// letting it hold a core until it finishes.
//
// WHY THIS EXISTS. The depth matrix measures UNBOUNDED quality (budget_ms 0), and the heuristic
// rollout leaf makes that regime pathological: one FiveColour H-arm game ran for 21.4 hours, and a
// 22,400-game run finished 88% of its work and then sat for 26.5 more hours on six games that could
// not change any conclusion. Roughly 1-2% of games carry about half of an arm's total cost. There
// was no way to stop one: BatchRunner's condemnation only stops FUTURE dispatch, and the mid-pass
// SearchBudget overrun guard rolls one PASS back and then keeps playing, so the GAME stays
// unbounded. Shipped play never enters this regime -- it is budgeted -- so this is an instrument for
// the measurement, not a change to how the engine plays.
//
// WHY IT COUNTS UNITS AND NOT SECONDS. SearchBudget exists precisely so that identical seed +
// budget does identical work on every machine. Keyed on wall time, the same game would be abandoned
// on one box and kept on another -- which breaks reproducibility and cross-machine pooling, the two
// properties the matrix is built on. In units, the set of abandoned games is a deterministic
// function of (deck, seed, depth, arm, limit) and is identical everywhere. That is what lets an
// abandoned game become a SKIP LIST the whole table can share.
//
// WHY IT IS SEPARATE FROM SearchBudget::Overrun. Overrun is a per-PASS ceiling whose meaning is
// "this pass over-ran its estimate, roll back to the last completed one" -- a legitimate, recorded
// event that leaves a playable line behind. Abandonment means "this game is VOID, do not report a
// result for it". Overloading one for the other would make the two indistinguishable in the very
// telemetry used to judge whether a cell is tractable. So the meter carries its own flag; Overrun
// consults it only to make the recursion unwind promptly.
//
// DISARMED BY DEFAULT. limit 0 means no ceiling, which is byte-identical to the engine before this
// existed: Add() still accumulates (so the counter is readable as pure telemetry) but nothing can
// ever be marked abandoned.
namespace gamework
{
// WHY a game can nonetheless be abandoned on WALL CLOCK -- see the two-stage backstop below. The
// paragraph above is still the rule; the backstop is the failsafe for when the rule stops working.

// Which rule ended a game. kUnits is the deterministic ceiling above and is the ONLY one that is a
// function of the data; the other two are the wall-clock backstop and are load-dependent by nature,
// which is exactly why they are reported apart (see BatchRunner's ABANDONED-WALL/-PREDICT lines).
enum class Cause
{
    kNone = 0,
    kUnits,     // hit the per-game work ceiling -- deterministic, reproducible
    kWall,      // stage 2: past the hard wall-clock cap
    kPredict    // stage 1: extrapolated to miss the hard cap by a wide margin, cut early
};

// One game runs on one thread, so the meter is thread_local: no atomics, no contention, and no
// cross-game leakage through a pooled worker thread as long as every entry point pairs Begin/End.
inline thread_local long long t_used      = 0;
inline thread_local long long t_limit     = 0;      // 0 == disarmed
inline thread_local bool      t_abandoned = false;
inline thread_local Cause     t_cause     = Cause::kNone;

// ---------------------------------------------------------------- CALIBRATION-WINDOW PUBLICATION
// A game whose cell is still CALIBRATING its relative ceiling (BatchRunner's CellCeiling) has no
// ceiling of its own yet -- that is what the calibration sample is for -- so before these existed it
// ran unbounded no matter how pathological it turned out to be. Measured on FiveColour V5: the
// cell's single worst game was 6,623x the cell median and 82.3% of everything the cell cost, and it
// was game 6, INSIDE the ten-game window, so the ceiling could not touch it.
//
// Two pointers close that. `t_publish` lets the freeze logic on another thread see how much work
// this game has done so far -- a LOWER bound, which is all that is needed to prove a still-running
// game sits above the sample's middle value and therefore cannot move the median. `t_late_limit`
// points at the cell's ceiling, so once it freezes, a calibration game already past it stops.
//
// BOTH ARE ONLY SET FOR CALIBRATION GAMES, so the hot path costs one predictable null check for
// every other game and for every non-batch caller. Publication is strided rather than per-call: the
// worst game measured here would otherwise store 614 million times.
inline thread_local std::atomic<long long>*       t_publish      = nullptr;
inline thread_local const std::atomic<long long>* t_late_limit   = nullptr;
inline thread_local long long                     t_publish_next = 0;
constexpr long long kPublishStride = 4096;

// ------------------------------------------------- THE TWO-STAGE WALL-CLOCK BACKSTOP (2026-09-20)
// A UNIT ceiling does not bound WALL CLOCK, and on the games this whole file exists to stop, the two
// come apart by an order of magnitude. Measured (docs/design/per-game-wall-clock-backstop.md): four
// Fungus games reached a 40M-unit ceiling after 2.5-4.3 HOURS, running at ~830-2,600 units/s against
// a 36,439 units/s median for slow games in the same run -- i.e. they landed exactly on the ceiling
// in its own currency while overshooting the intended bound by 4-50x in time. Every one was then
// abandoned and its work discarded, so the 4.26 h game produced nothing at all. Worse, a cell whose
// ceiling is DISARMED (the matrix driver's skip_capped: past the skip cap all three abandon_* fields
// go to 0) has no bound of any kind, in either currency.
//
// USER 2026-09-19: "we didn't want to cap it at 1 hour in a non-deterministic way, but we did want
// to have a guard when it is clearly overshooting that target to prevent 4h games."
//
// TWO STAGES, because one cap cannot be both safe and cheap (USER 2026-09-20):
//   STAGE 1  PREDICTIVE CUT, at t_predict_sec. Extrapolate from the work done so far: at this game's
//            observed unit rate, can it still reach its unit ceiling before the hard cap? If it
//            misses by a wide margin, cut now rather than spend two more hours proving it.
//   STAGE 2  HARD CAP, at t_hard_sec. Unconditional. Catches anything whose rate changed after the
//            stage-1 check, so no game runs unbounded whatever the extrapolation believed.
//
// WHY STAGE 1 IS REPRODUCIBILITY-NEUTRAL, which is the objection that kept this unbuilt. A
// wall-clock trigger is load-dependent, so on its own it would stop the skip list -- which every
// other cell of the matrix filters on -- from being a function of the data. But stage 1 only ever
// cuts a game that CANNOT reach its ceiling before stage 2 would have killed it anyway, and both
// stages produce the same ABANDONED verdict, so the skip list is identical either way. Stage 1
// changes only how many hours were burned reaching it. The reproducibility question is therefore
// entirely about stage 2's threshold, and stage 1 is free.
//
// THE ONE WAY STAGE 1 CAN BE WRONG is cutting a game that would have COMPLETED -- not merely reached
// the ceiling -- between the two stages, i.e. one whose rate was about to recover. Two things make
// that a non-event: reaching the stage-1 check AT ALL already means the game is pathological (a
// healthy game at the median rate would have passed a 40M ceiling after ~18 minutes), and the cut
// demands kPredictSafety x the hard cap, not merely more than it. Prefer to let stage 2 do it when
// in doubt -- the gap between the two stages IS the safety factor.
//
// NO CEILING => NO STAGE 1. t_limit is the numerator of the extrapolation, so a game with no unit
// ceiling cannot be predicted about and only stage 2 applies to it. That is the right answer rather
// than a gap: inventing a prediction for an unbounded game would just be a second, shorter hard cap
// wearing the word "predictive", and stage 2 already bounds it.
inline thread_local std::chrono::steady_clock::time_point t_t0{};
inline thread_local double    t_hard_sec    = 0.0;    // 0 == disarmed
inline thread_local double    t_predict_sec = 0.0;    // 0 == disarmed
inline thread_local bool      t_predicted   = false;  // stage 1 evaluated once, then never again
inline thread_local long long t_clock_next  = 0;      // 0 == no deadline armed; see kClockStride
// Seconds into the game at which it was ABANDONED (0 = it has not been, or no deadline is armed -- the
// clock is only read with one). Reported beside the game's final elapsed time (BatchRunner's ABANDONED*
// lines) so the two latencies a late cut can hide in read apart: noticing the cap (this minus the cap)
// and unwinding after it (elapsed minus this). Set for a unit-ceiling cut too when a deadline is armed,
// which is what lets the unwind be measured at a DETERMINISTIC point (abandon_units).
inline thread_local double    t_fired_sec   = 0.0;
// The clock is read on a UNIT stride, never per call: Add() runs once per simulated turn-step, and
// the worst game on record would otherwise read steady_clock 614 million times.
//
// 64, NOT 4096 (2026-10-21). The old 4096 was sized off "~5 s of granularity even at the 830 units/s
// pathological rate", and that premise was the bug: a stride in UNITS is a bound in TIME only while the
// unit rate is bounded below, and on AccomplishedAlchemist's value-leaf label games it is not -- between
// two charged units sits a Combo Off route walk with its continuation derivations, uncharged. Measured
// (MTG_BACKSTOP_GAP_STATS) on a quiet box: up to 81 s between two 4096-unit reads, on games the loaded
// generation box ran ~8x slower. The uncharged loops now read the deadline themselves (Poll, below), and
// this stride bounds whatever uncharged work is left between charges. The price of 64 is nothing: one
// steady_clock read (~25-50 ns, vDSO) per 64 Adds, i.e. ~0.3 s across the 614M-unit game above, and only
// in a game with a deadline armed -- with none, t_clock_next stays 0 and the stride is never consulted.
constexpr long long kClockStride = 64;
// Stage 1 cuts only when the projection misses the hard cap by THIS multiple. 1.5 is deliberately
// generous: the games this exists for project at 3-4x the cap (a 40M ceiling at 830 units/s implies
// 13.4 h against a 3.5 h cap), so the margin costs nothing on the real pathology while keeping a
// merely-slow game for stage 2 to judge on its own evidence.
constexpr double kPredictSafety = 1.5;

// MTG_BACKSTOP_GAP_STATS=1 (DIAGNOSTIC, print-only, default OFF). Per game with a deadline armed: the
// longest WALL GAP between two reads of the deadline, i.e. how late the cap can be noticed wherever in the
// game it lands -- for this binary's schedule (every CheckDeadline: Add's kClockStride and every Poll) AND
// for the schedule the meter had before 2026-10-21 (Add alone, every 4096 units), shadowed from the same
// game. The polls change no play, so both numbers describe ONE trajectory: a before/after measurement of
// the detection latency over a whole game without having to land a cap inside a stall. BatchRunner prints
// it as `[backstop-gaps]`. Reads no extra clock. Arm a cap past the game's length to measure a whole game
// (MTG_MAX_GAME_WALL_SEC=1000000).
inline bool GapStatsOn() { static const bool v = EnvOn("MTG_BACKSTOP_GAP_STATS"); return v; }
constexpr long long kLegacyClockStride = 4096;   // the pre-2026-10-21 kClockStride, shadowed (diagnostic)
struct GapStats
{
    double    last = 0.0, max = 0.0, max_at = 0.0;           // this binary's reads
    double    lg_last = 0.0, lg_max = 0.0, lg_max_at = 0.0;  // the legacy 4096-unit schedule
    long long lg_next = 0;
};
inline thread_local GapStats t_gap;
inline const GapStats& Gaps() { return t_gap; }

// Arm (or disarm, with 0) the meter for one game and reset the counter.
inline void Begin(long long limit_units)
{
    t_used = 0;
    t_limit = limit_units > 0 ? limit_units : 0;
    t_abandoned = false;
    t_cause = Cause::kNone;
    t_publish = nullptr;
    t_late_limit = nullptr;
    t_publish_next = 0;
    t_hard_sec = 0.0;
    t_predict_sec = 0.0;
    t_predicted = false;
    t_clock_next = 0;
    t_fired_sec = 0.0;
}

// Arm the wall-clock backstop for this game. Call AFTER Begin (which disarms it), the same shape as
// PublishTo. Either bound may be 0 to leave that stage off; both 0 is the default everywhere and is
// byte-identical to the engine before this existed -- no clock is ever read.
inline void ArmDeadline(double predict_sec, double hard_sec)
{
    t_predict_sec = predict_sec > 0.0 ? predict_sec : 0.0;
    t_hard_sec    = hard_sec    > 0.0 ? hard_sec    : 0.0;
    t_predicted   = false;
    if (t_predict_sec <= 0.0 && t_hard_sec <= 0.0) { t_clock_next = 0; return; }
    t_t0 = std::chrono::steady_clock::now();
    t_clock_next = t_used + kClockStride;
    t_gap = GapStats{};
    t_gap.lg_next = t_used + kLegacyClockStride;
}

// Seconds this game has been running. Only meaningful with a deadline armed (t_t0 is set there);
// read it before End(), exactly as Used() is.
inline double Elapsed()
{
    if (t_clock_next == 0 && t_predict_sec <= 0.0 && t_hard_sec <= 0.0) { return 0.0; }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t_t0).count();
}

// The strided slow path of Add(). Out of line from the hot test so the common case stays one
// integer compare.
inline void CheckDeadline()
{
    t_clock_next = t_used + kClockStride;
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_t0).count();
    if (GapStatsOn())   // MTG_BACKSTOP_GAP_STATS (diagnostic): see GapStats
    {
        GapStats& g = t_gap;
        if (el - g.last > g.max) { g.max = el - g.last; g.max_at = el; }
        g.last = el;
        if (t_used >= g.lg_next)
        {
            if (el - g.lg_last > g.lg_max) { g.lg_max = el - g.lg_last; g.lg_max_at = el; }
            g.lg_last = el;
            g.lg_next = t_used + kLegacyClockStride;
        }
    }
    // STAGE 2 first: it is unconditional, so a game past the hard cap is reported as kWall even if
    // the stage-1 check happens to land on the same stride.
    if (t_hard_sec > 0.0 && el >= t_hard_sec)
    {
        t_abandoned = true;
        if (t_cause == Cause::kNone) { t_cause = Cause::kWall; t_fired_sec = el; }
        return;
    }
    if (t_predicted || t_predict_sec <= 0.0 || el < t_predict_sec) { return; }
    t_predicted = true;   // evaluated once; a game that survives it is stage 2's problem
    if (t_limit <= 0 || t_used <= 0 || t_hard_sec <= 0.0) { return; }
    const double projected = el * (static_cast<double>(t_limit) / static_cast<double>(t_used));
    if (projected > t_hard_sec * kPredictSafety)
    {
        t_abandoned = true;
        if (t_cause == Cause::kNone) { t_cause = Cause::kPredict; t_fired_sec = el; }
    }
}

// --------------------------------------------------- POLLING FROM AN UNCHARGED LOOP (2026-10-21)
// CheckDeadline runs from Add() alone, i.e. only when work is CHARGED. A loop that charges no unit
// therefore never reads the clock, and the backstop cannot fire inside it however long it runs.
// AccomplishedAlchemist's Combo Off ROUTE WALK is such a loop (aaroute::Dfs: provider sub-search,
// never calls SearchBudget::Consume), and so are the breakpoint-continuation DERIVATIONS its applies
// trigger (BpDeriveContinuationList -> EnumeratePlansWithLand under g_bp_enum_depth: 17k plans per
// list on average, 287k at the worst). Its value-leaf phase A runs the label games with a 4 h hard
// cap (MTG_MAX_GAME_WALL_SEC=14400), and they ran straight through it: gi137 of
// accomplishedalchemist_rows_00750_000 at 7.4 h and still going, others abandoned only at 16,742 s.
// USER 2026-10-21: "they should be cutting off naturally". A late cut is late in one of two places,
// and both were unbounded: NOTICING the cap (this poll and kClockStride) and UNWINDING after it (the
// search loops that keep applying plans once their children have stopped -- they now leave on
// gamework::Abandoned; see FSLineWin's plan loop, the label ladder, TurnSolver's AnytimeStop).
// docs/design/per-game-wall-clock-backstop.md, "Uncharged loops", has the measurements.
//
// Poll() reads the SAME deadline from inside such a loop. The loop then unwinds through the cut path
// it already has (the route walk's `cut`, a derivation's interrupted flag, the greedy walk's
// walk_exhausted), exactly as it does for a unit ceiling. NO STRIDE HERE: a coarse loop (one route-walk
// apply costs milliseconds) calls it every iteration; a per-visit loop keeps its own counter and calls
// it every N visits (TurnSolver's kWalkAbandonStride), because the cheapest stride is the one the hot
// loop already pays for.
//
// INERT WHERE NOTHING IS ARMED, by construction. With no wall deadline (t_clock_next == 0: every GT
// tier, the viewer, shipped play) it returns t_abandoned -- false unless a unit ceiling fired -- and
// reads no clock. A game that is never abandoned takes no branch a caller adds on its result; a game
// that IS abandoned is VOID (RunGame -> kAbandoned, reduce_job drops it), so where its loops noticed
// cannot move any reported result. Callers must still not CACHE anything computed after the cut
// (each call site says how it avoids that).
inline bool Poll()
{
    if (t_abandoned) { return true; }
    if (t_clock_next == 0) { return false; }   // no wall deadline armed: nothing to read
    CheckDeadline();
    return t_abandoned;
}

// Publish this game's progress (and honour a ceiling that freezes while it runs). Call after Begin.
inline void PublishTo(std::atomic<long long>* progress, const std::atomic<long long>* late_limit)
{
    t_publish = progress;
    t_late_limit = late_limit;
    t_publish_next = kPublishStride;
    if (t_publish) { t_publish->store(0, std::memory_order_relaxed); }
}

// A unit-ceiling cut's moment, for the report (see t_fired_sec). Once per game, and only with a deadline
// armed (t_t0 is set there), so a unit-ceiling run without one reads no clock -- as before.
inline void NoteUnitsFired()
{
    if (t_clock_next == 0) { return; }
    t_fired_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_t0).count();
}

// Fold in work units. Called from SearchBudget::Consume, i.e. once per simulated turn-step,
// so this is the same currency the user-facing virtual-ms budget is denominated in.
inline void Add(long long n)
{
    t_used += n;
    if (t_limit > 0 && t_used >= t_limit)
    {
        t_abandoned = true;
        if (t_cause == Cause::kNone) { t_cause = Cause::kUnits; NoteUnitsFired(); }
    }
    // One integer compare when no deadline is armed, which is every caller that does not opt in.
    if (t_clock_next != 0 && t_used >= t_clock_next) { CheckDeadline(); }
    if (t_publish != nullptr && t_used >= t_publish_next)
    {
        t_publish->store(t_used, std::memory_order_relaxed);
        t_publish_next = t_used + kPublishStride;
        // A ceiling that froze after this game started applies to it too. Sound because the freeze
        // only happens once every running calibration game is PROVEN to sit above the sample's
        // middle value, so abandoning one cannot change the median that was frozen from it.
        if (t_late_limit != nullptr)
        {
            const long long late = t_late_limit->load(std::memory_order_relaxed);
            if (late > 0 && t_used >= late)
            {
                t_abandoned = true;
                if (t_cause == Cause::kNone) { t_cause = Cause::kUnits; NoteUnitsFired(); }
            }
        }
    }
}

inline bool      Abandoned()    { return t_abandoned; }
inline long long Used()         { return t_used; }
inline long long Limit()        { return t_limit; }
inline Cause     AbandonCause() { return t_cause; }
inline double    FiredAt()      { return t_fired_sec; }   // see t_fired_sec; read before End()

// For the runner's report. "UNITS" is the deterministic verdict; the other two are the backstop and
// a reader must be able to tell them apart at a glance -- a run that fired one is not reproducible
// from the data alone, and the log line is the only place that is recorded.
inline const char* CauseName(Cause c)
{
    switch (c)
    {
        case Cause::kUnits:   return "UNITS";
        case Cause::kWall:    return "WALL";
        case Cause::kPredict: return "PREDICT";
        default:              return "NONE";
    }
}

// Disarm at the end of a game. Leaves t_used readable for reporting.
inline void End()
{
    t_limit = 0;
    t_abandoned = false;
    t_cause = Cause::kNone;
    t_hard_sec = 0.0;
    t_predict_sec = 0.0;
    t_clock_next = 0;
    t_fired_sec = 0.0;
}
}   // namespace gamework
