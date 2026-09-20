#pragma once

#include <atomic>
#include <chrono>

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
// The clock is read on a UNIT stride, never per call: Add() runs once per simulated turn-step, and
// the worst game on record would otherwise read steady_clock 614 million times. 4096 units is
// ~5 s of granularity even at the 830 units/s pathological rate and ~0.1 s at a healthy one, which
// is nothing against a bound denominated in hours.
constexpr long long kClockStride = 4096;
// Stage 1 cuts only when the projection misses the hard cap by THIS multiple. 1.5 is deliberately
// generous: the games this exists for project at 3-4x the cap (a 40M ceiling at 830 units/s implies
// 13.4 h against a 3.5 h cap), so the margin costs nothing on the real pathology while keeping a
// merely-slow game for stage 2 to judge on its own evidence.
constexpr double kPredictSafety = 1.5;

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
    // STAGE 2 first: it is unconditional, so a game past the hard cap is reported as kWall even if
    // the stage-1 check happens to land on the same stride.
    if (t_hard_sec > 0.0 && el >= t_hard_sec)
    {
        t_abandoned = true;
        if (t_cause == Cause::kNone) { t_cause = Cause::kWall; }
        return;
    }
    if (t_predicted || t_predict_sec <= 0.0 || el < t_predict_sec) { return; }
    t_predicted = true;   // evaluated once; a game that survives it is stage 2's problem
    if (t_limit <= 0 || t_used <= 0 || t_hard_sec <= 0.0) { return; }
    const double projected = el * (static_cast<double>(t_limit) / static_cast<double>(t_used));
    if (projected > t_hard_sec * kPredictSafety)
    {
        t_abandoned = true;
        if (t_cause == Cause::kNone) { t_cause = Cause::kPredict; }
    }
}

// Publish this game's progress (and honour a ceiling that freezes while it runs). Call after Begin.
inline void PublishTo(std::atomic<long long>* progress, const std::atomic<long long>* late_limit)
{
    t_publish = progress;
    t_late_limit = late_limit;
    t_publish_next = kPublishStride;
    if (t_publish) { t_publish->store(0, std::memory_order_relaxed); }
}

// Fold in work units. Called from SearchBudget::Consume, i.e. once per simulated turn-step,
// so this is the same currency the user-facing virtual-ms budget is denominated in.
inline void Add(long long n)
{
    t_used += n;
    if (t_limit > 0 && t_used >= t_limit)
    {
        t_abandoned = true;
        if (t_cause == Cause::kNone) { t_cause = Cause::kUnits; }
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
                if (t_cause == Cause::kNone) { t_cause = Cause::kUnits; }
            }
        }
    }
}

inline bool      Abandoned()    { return t_abandoned; }
inline long long Used()         { return t_used; }
inline long long Limit()        { return t_limit; }
inline Cause     AbandonCause() { return t_cause; }

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
}
}   // namespace gamework
