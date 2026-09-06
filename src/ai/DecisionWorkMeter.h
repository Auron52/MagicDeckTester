#pragma once

// Per-DECISION work ceiling (MTG_DECISION_WORK_X, default 0 = disarmed = byte-identical).
//
// WHY. SearchBudget bounds one PASS and gamework bounds one GAME, but a single real decision's
// TOTAL spend -- base pass + iterative-deepening re-passes + escalation probes + their
// sub-budgets -- had no ceiling at all. On a Melira infinite-combo board that legally multiplies
// into minutes per decision (suite game s1033/gi32: 289 s for a t5 win at --budget-ms 10, i.e.
// thousands of times its nominal budget), which is exactly the class that makes the deck's
// generation and suite runs intractable. The tail cannot be bounded pass-by-pass, because each
// pass individually behaves; only the decision's sum explodes.
//
// WHAT. A thread_local counter armed at the ROOT of a real budgeted decision with
// limit = base_budget_units * MTG_DECISION_WORK_X. Every SearchBudget::Consume feeds it (same
// single-count contract as gamework: the recursion consumes from one budget per node).
// SearchBudget::Overrun() consults it, so when the ceiling trips, running passes bail through
// the EXISTING mid-pass overrun path and iterative deepening commits the deepest completed
// pass -- a playable line, deterministically chosen (unit-counted, no wall clock). This is a
// SOFT stop (the decision still answers), unlike gamework abandonment (the game is void).
//
// Deterministic: identical seed + budget + multiplier trips at the identical unit on every
// machine. Disarmed (multiplier 0/unset) nothing is ever tripped and play is byte-identical.
namespace decisionwork
{
inline thread_local long long t_used  = 0;
inline thread_local long long t_limit = 0;   // 0 == disarmed

inline void Add(long long n) { t_used += n; }
inline bool Armed() { return t_limit > 0; }
inline bool Exceeded() { return t_limit > 0 && t_used >= t_limit; }

// Arm (limit_units <= 0 disarms) and reset for one decision. Only the decision ROOT calls this.
inline void Begin(long long limit_units)
{
    t_used  = 0;
    t_limit = limit_units > 0 ? limit_units : 0;
}

// RAII: arm at a decision root, restore the outer scope's meter on exit (label/measurement
// paths nest decisions; the outer one must get its own accounting back). limit <= 0 is a
// TRUE no-op -- it must not Begin(), or a nested non-root frame would zero the root's counter
// and its spend would vanish from the root's ceiling on restore.
struct Scope
{
    bool      armed;
    long long prev_used, prev_limit;
    explicit Scope(long long limit_units)
        : armed(limit_units > 0), prev_used(t_used), prev_limit(t_limit)
    { if (armed) { Begin(limit_units); } }
    ~Scope() { if (armed) { t_used = prev_used; t_limit = prev_limit; } }
};
}
