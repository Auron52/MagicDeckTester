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
#include <algorithm>
#include "../core/WorkCounters.h"
#include "../core/EnvFlags.h"
#include "ValueArm.h"

// t_reads: every budget-predicate EVALUATION that could have answered "cut" (SearchBudget::Exhausted on a finite
// budget, SearchBudget::Overrun, an ARMED decisionwork::PricedExceeded), whatever it answered. A cached computation
// that read one depends on budget STATE even when it charged nothing, so a hit is exact only where the same reads
// would answer the same way (cachecharge::Cost::budget_read; MTG_CACHE_READ_GUARD). budgetbind::t_binds alone cannot
// see this: it counts only the reads that answered true, so an entry stored before a cut looks state-free.
namespace budgetbind { inline thread_local unsigned long long t_reads = 0; }
namespace decisionwork
{
inline thread_local long long t_used  = 0;
inline thread_local long long t_limit = 0;   // 0 == disarmed
// Residual greedy-walk visits toward the next charged unit when the walk bills THIS meter instead
// of a SearchBudget (MTG_SOLVE_CHARGE_W > 1; see GreedyChargeGuard). It lives here, rather than as
// a bare thread_local at the charge site, so that it is scoped to a DECISION: a residual that
// survived the decision would carry visits between GAMES on a reused batch worker thread, making
// the charge -- and so the play it bounds -- depend on which games that thread happened to run
// first. Begin() zeroes it and Scope restores it, exactly as for t_used.
inline thread_local int       t_charge_acc = 0;

// PRICED CEILING (MTG_DECISION_PRICED_X, default 100 -- USER 2026-11-03 after A/B v3: X=100 moved no win turn in
// ~31.4k held-out games or the 45-game tail corpus, held-out wall -5%; X<=50 lost turns on heavy games; 0 = off). The unit ceiling above counts budget units,
// which miss most of a decision's real cost on some decks (plan enumeration, applies, mana payment are
// charged nothing or one unit regardless of size; see core/WorkCounters.h). This second ceiling counts
// PRICED work -- every kind, charged or not -- from workctr::t_priced, so it bounds what a decision
// actually costs. Trip path: SearchBudget::Overrun (NOT Exceeded, see there) -> running passes roll back, the
// ladder commits the deepest COMPLETED pass; same scoping. Inert at an unlimited budget: only a
// budgeted decision root arms it. Kept apart from Armed(), which gates the unit meter's own billing.
inline thread_local long long t_pr_start = 0;
inline thread_local long long t_pr_limit = 0;   // 0 == disarmed
inline bool PricedExceededQuiet() { return t_pr_limit > 0 && workctr::t_priced - t_pr_start >= t_pr_limit; }
inline bool PricedExceeded() { if (t_pr_limit > 0) { ++budgetbind::t_reads; } return PricedExceededQuiet(); }
// The PRICED START GATE (FullSearchLine's ladder): decline to START a pass whose projected priced cost does not fit
// the decision's remaining priced allowance. Per-job arm "decision_priced_gate" (ValueArm) else MTG_DECISION_PRICED_GATE
// (default OFF since 2026-11-03: it projects the next pass as prev x prev/prev2, which overshoots when the growth ratio
// swings, and declined a pass the ceiling never cut -- FiveColour regression d5 s3003 gi103 T6 -> T7 at X=100, while gate
// off == the uncapped game, units included. A/B v3 X=50 gate on vs off: same win turns, held-out wall 8,511 vs 8,461 s,
// tail corpus 5,160 vs 5,143 s -- it bought nothing). Only consulted while the priced ceiling is armed.
inline bool PricedGateOn()
{
    static const bool env = EnvOn("MTG_DECISION_PRICED_GATE", false);
    const int a = valuearm::t_arm.decision_priced_gate;
    return a >= 0 ? a != 0 : env;
}
inline thread_local long long t_pr_gate_refusals = 0;   // diagnostic: passes the priced gate declined (this thread)

inline void Add(long long n) { t_used += n; }
inline bool Armed() { return t_limit > 0; }
// UNITS ONLY. The priced trip is NOT folded in here: Exceeded() also means "the unit meter is spent" to the
// plan caches (no store, no served hit), the unbudgeted rollouts (instant no-win) and the no-win stores, and
// routing the priced trip through it made a tripped decision lose its caches and score every remaining
// rollout as a loss -- measured 2026-11-03, Goblins 2HG seed 2032 gi30 at X=10: 13.5 s priced limit, 104 s
// spent (23.6 s with no ceiling), 230k units vs 141k. The priced trip reaches the search through
// SearchBudget::Overrun() only (the per-pass ceiling's rollback path), see SearchBudget.h.
inline bool Exceeded() { return t_limit > 0 && t_used >= t_limit; }

// Arm (limit_units <= 0 disarms) and reset for one decision. Only the decision ROOT calls this.
inline void Begin(long long limit_units)
{
    t_used       = 0;
    t_limit      = limit_units > 0 ? limit_units : 0;
    t_charge_acc = 0;
}

// RAII: arm at a decision root, restore the outer scope's meter on exit (label/measurement
// paths nest decisions; the outer one must get its own accounting back). limit <= 0 is a
// TRUE no-op -- it must not Begin(), or a nested non-root frame would zero the root's counter
// and its spend would vanish from the root's ceiling on restore.
struct Scope
{
    bool      armed;
    long long prev_used, prev_limit;
    int       prev_acc;
    explicit Scope(long long limit_units)
        : armed(limit_units > 0), prev_used(t_used), prev_limit(t_limit), prev_acc(t_charge_acc)
    { if (armed) { Begin(limit_units); } }
    ~Scope() { if (armed) { t_used = prev_used; t_limit = prev_limit; t_charge_acc = prev_acc; } }
};

// RAII for the PRICED ceiling, armed at the same decision roots (limit_ns <= 0 is a true no-op).
struct PricedScope
{
    bool      armed;
    long long prev_start, prev_limit;
    explicit PricedScope(long long limit_ns)
        : armed(limit_ns > 0), prev_start(t_pr_start), prev_limit(t_pr_limit)
    { if (armed) { t_pr_start = workctr::t_priced; t_pr_limit = limit_ns; } }
    ~PricedScope() { if (armed) { t_pr_start = prev_start; t_pr_limit = prev_limit; } }
};
// The priced limit for a decision whose budget is `limit_units`: X x the budget valued at a REFERENCE
// cost per unit (MTG_DECISION_PRICED_REF_NS, default 25,000 ns -- about the median deck's wall per
// unit on the 2026-11-03 regression tier), i.e. X = how many budgets' worth of real work one decision
// may spend. 0 when off or unbudgeted.
inline long long PricedLimitNs(long long limit_units);
inline long long PricedLimitNs(long long limit_units)
{
    static const long long env_x = EnvInt("MTG_DECISION_PRICED_X", 100);
    static const long long ref   = EnvInt("MTG_DECISION_PRICED_REF_NS", 25000);
    const long long x = valuearm::t_arm.decision_priced_x >= 0 ? valuearm::t_arm.decision_priced_x : env_x;   // per-job arm wins
    if (x <= 0 || limit_units <= 0) { return 0; }
    return limit_units * ref * x;
}
}
// PRICED BUDGET (MTG_PRICED_BUDGET, default OFF while it is measured; docs/design/budget-rebalance-checkpoint.md).
// A finite budget is spent in REAL COST, not in units: every charge bills the priced work done on this thread since
// the previous charge (workctr::t_priced -- plan enumeration, applies, mana payment, state keys, rollout steps,
// charged or not) plus kInteriorNs per unit charged, converted at UnitNs() ns per unit (MTG_PRICED_UNIT_NS, default
// 28,300 = the median deck's priced ns per unit on the 2026-11-04 regression fit, so that deck searches as much as
// before). A deck whose units hide heavy uncharged work (EDF ~103k ns/unit, Dragonstorm ~64k, Kitty ~55k) gets
// proportionally fewer units per budget-ms; a light one (Goblins ~15k) more. RE-PRICES, never removes: an
// UNLIMITED budget is charged exactly as before (Bill returns n), so everything findable at a large budget still is.
// Deterministic and residency-independent: t_priced is integer prices x counts on this thread, the plan caches'
// priced ledger makes it as-if-unlimited-cache, and tier-2 hits must charge what a recompute does (Tier2Overflow.h).
// The kInteriorNs floor keeps every charge site progressing (a charge never bills less than its units' own price).
namespace pricedbudget
{
// R in ns per unit for this job: the per-job arm (valuearm priced_unit_ns) wins, else the env; 0 = off.
inline long long UnitNs()
{
    static const long long env = EnvOn("MTG_PRICED_BUDGET", false) ? std::max(1, EnvInt("MTG_PRICED_UNIT_NS", 28300)) : 0;
    const int a = valuearm::t_arm.priced_unit_ns;
    return a >= 0 ? a : env;
}
inline bool On() { return UnitNs() > 0; }
inline thread_local long long t_wm  = 0;   // t_priced already billed (or dropped) on this thread
inline thread_local long long t_acc = 0;   // billed ns not yet worth a whole unit
// > 0 inside a FINITE decision root's Scope. Billing happens only there: a finite sub-budget that an UNLIMITED root
// builds for a probe pass is charged units exactly as before, so an unlimited search is untouched by construction.
inline thread_local int       t_active = 0;
inline bool Active() { return t_active > 0; }
// Units to charge a FINITE budget for a charge of n units.
inline long long Bill(long long n)
{
    long long d = workctr::t_priced - t_wm;
    if (d < 0) { d = workctr::t_priced; }   // t_priced was reset (a new game) since the last bill
    t_wm = workctr::t_priced;
    const long long ns = t_acc + n * workctr::kInteriorNs + d;
    const long long u = ns / UnitNs();
    t_acc = ns - u * UnitNs();
    return u;
}
// Work done where no finite budget is charged (an unlimited budget, a null budget) is dropped, not carried into
// the next finite one.
inline void Drop() { t_wm = workctr::t_priced; }
// RAII at a decision ROOT: work done before the decision is not billed to it, and the decision's own work is not
// billed to an enclosing one (nested label / measurement roots) -- the enclosing pending work is kept as it was.
struct Scope
{
    bool      on;
    long long prev_pending, prev_acc;
    explicit Scope(bool root) : on(root && On()), prev_pending(0), prev_acc(0)
    {
        if (!on) { return; }
        prev_pending = workctr::t_priced - t_wm;
        if (prev_pending < 0) { prev_pending = 0; }
        prev_acc = t_acc;
        t_wm = workctr::t_priced;
        t_acc = 0;
        ++t_active;
    }
    ~Scope() { if (on) { --t_active; t_wm = workctr::t_priced - prev_pending; t_acc = prev_acc; } }
};
}
