#pragma once

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
// One game runs on one thread, so the meter is thread_local: no atomics, no contention, and no
// cross-game leakage through a pooled worker thread as long as every entry point pairs Begin/End.
inline thread_local long long t_used      = 0;
inline thread_local long long t_limit     = 0;      // 0 == disarmed
inline thread_local bool      t_abandoned = false;

// Arm (or disarm, with 0) the meter for one game and reset the counter.
inline void Begin(long long limit_units)
{
    t_used = 0;
    t_limit = limit_units > 0 ? limit_units : 0;
    t_abandoned = false;
}

// Fold in work units. Called from SearchBudget::Consume, i.e. once per simulated turn-step,
// so this is the same currency the user-facing virtual-ms budget is denominated in.
inline void Add(long long n)
{
    t_used += n;
    if (t_limit > 0 && t_used >= t_limit) { t_abandoned = true; }
}

inline bool      Abandoned() { return t_abandoned; }
inline long long Used()      { return t_used; }
inline long long Limit()     { return t_limit; }

// Disarm at the end of a game. Leaves t_used readable for reporting.
inline void End() { t_limit = 0; t_abandoned = false; }
}   // namespace gamework
