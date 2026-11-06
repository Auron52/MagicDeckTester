#pragma once
// PER-GAME WORK COUNTERS: how much of each KIND of search work a game did,
// charged to the budget or not. The input for pricing the budget's unit against real cost (see
// docs/design/budget-rebalance-checkpoint.md): a per-game fit of wall time on these counts says what
// each kind of work costs and how much of it the unit never sees. Thread_local increments only;
// reset at game start (TurnSolver::ClearPerGameCaches); written per game by BatchRunner under
// MTG_TIER1_STATS beside the tier-1 ledger.
namespace workctr
{
enum Kind : int
{
    kStepCharged = 0,   // rollout turn-step billed to a budget (unitsite rollout_step)
    kStepFree,          // rollout turn-step with no budget (the plan-scoring samples)
    kGreedyVisit,       // greedy subset-walk visit (SolveUncached)
    kEnumCall,          // EnumeratePlans call
    kPlans,             // plans produced by EnumeratePlansWithLand
    kOdoVisit,          // enumeration odometer visits (EnumChargeScope)
    kApply,             // ApplyPlanDirect call
    kPay,               // TapForCostShared call (a mana-payment search; EDF: 38% of a slow game's time)
    kSimKey,            // BuildSimKeyBase call (state key for the TT / line caches / memos)
    kPlanEmit,          // plan built by EnumeratePlans, before sorting/deduplication
    kKinds
};
inline const char* KindName(int k)
{
    static const char* n[kKinds] = { "step_c", "step_f", "greedy", "enum", "plans", "odo", "apply", "pay", "simkey", "plan_emit" };
    return (k >= 0 && k < kKinds) ? n[k] : "?";
}
// PRICES, in nanoseconds of reference work per event: a non-negative per-game fit of wall clock on these
// counts (scripts/cost_fit.py over the regression tier, 2026-11-03; this box, Release). They turn the
// counts into ONE currency that tracks real cost across decks -- which the search unit (one per
// simulated turn-step / plan applied) does not: units-only mispredicts a game's wall by up to ~6000x,
// the priced sum by ~3x at p95. Deterministic (integer constants x counts), so anything bounded in this
// currency trips at the same event on every machine. The final plan_emit price is a tail calibration
// added after that fit; the other prices below are unchanged.
// Fit 2026-11-04 (scripts/cost_fit.py on the regression tier, 13,525 searched games >= 50 ms, NNLS weighted by
// 1/ms; QUIET box, HEAD fa6aeb086 src, 32 threads, nothing else running -- replaces the contended 2026-11-03 fit).
// Shares of fitted time: apply 25.2%, simkey 23.8%, pay 19.6%, enum 14.1%, plans 7.9%, step 4.5%, odo 2.2%,
// interior unit 1.8% (586 ns; kInteriorNs below, not a work kind), greedy 0.8%. actual/pred p95 1.57 overall and
// 1.78 on the slowest 1% (units-only 3.29 / 7.45); per-deck median ratio 0.81-1.31 (units-only 1.02-4.69).
inline constexpr long long kPriceNs[kKinds] = {
    21426,   // step_c
    21426,   // step_f   (same work as a charged step)
      569,   // greedy
    18455,   // enum
     2866,   // plans
      511,   // odo
     8097,   // apply
     2561,   // pay
     3749,   // simkey
    10000,   // plan_emit; Melira's 46M emitted plans took 548 s (~11.9 us each, including other work)
};
// The fitted price of one CHARGED budget unit beyond the work kinds above (the fit's `interior` regressor).
inline constexpr long long kInteriorNs = 586;
inline thread_local long long t_n[kKinds] = {};
inline thread_local long long t_priced = 0;   // running sum of price x count this game (ns)
// MUTE (> 0: count nothing). Held by the plan caches' PRICED LEDGER (TurnSolver.cpp, namespace pledger) over a
// recompute that the deterministic virtual cache would have served as a hit: there the work never ran, so it must not
// reach t_priced (the priced decision ceiling reads it) or the per-kind counts. Zero outside such a recompute.
inline thread_local int t_mute = 0;
inline void Inc(int k, long long n = 1) { if (t_mute != 0) { return; } t_n[k] += n; t_priced += kPriceNs[k] * n; }
inline void BeginGame() { for (int k = 0; k < kKinds; ++k) { t_n[k] = 0; } t_priced = 0; }
}
