#pragma once
// PER-GAME WORK COUNTERS (diagnostic, play-inert): how much of each KIND of search work a game did,
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
    kKinds
};
inline const char* KindName(int k)
{
    static const char* n[kKinds] = { "step_c", "step_f", "greedy", "enum", "plans", "odo", "apply", "pay", "simkey" };
    return (k >= 0 && k < kKinds) ? n[k] : "?";
}
// PRICES, in nanoseconds of reference work per event: a non-negative per-game fit of wall clock on these
// counts (scripts/cost_fit.py over the regression tier, 2026-11-03; this box, Release). They turn the
// counts into ONE currency that tracks real cost across decks -- which the search unit (one per
// simulated turn-step / plan applied) does not: units-only mispredicts a game's wall by up to ~6000x,
// the priced sum by ~3x at p95. Deterministic (integer constants x counts), so anything bounded in this
// currency trips at the same event on every machine.
// Fit 2026-11-03 (scripts/cost_fit.py on the regression tier, 16,369 searched games >= 50 ms, NNLS weighted
// by 1/ms; wall CONTENDED by concurrent builds, so treat as provisional). Shares of fitted time: simkey 41.5%,
// enum 16.7%, apply 14.6%, pay 13.8%, plans 4.8%, step 3.6%, odo 3.4%, greedy 0.8% (interior unit 0.3 us,
// 0.8%, not a work kind). Slowest 1% of games: actual/pred p95 3.7 (units-only 15.4).
inline constexpr long long kPriceNs[kKinds] = {
    20322,   // step_c
    20322,   // step_f   (same work as a charged step)
      697,   // greedy
    26807,   // enum
     2098,   // plans
      967,   // odo
     5560,   // apply
     2170,   // pay
     7768,   // simkey
};
inline thread_local long long t_n[kKinds] = {};
inline thread_local long long t_priced = 0;   // running sum of price x count this game (ns)
inline void Inc(int k, long long n = 1) { t_n[k] += n; t_priced += kPriceNs[k] * n; }
inline void BeginGame() { for (int k = 0; k < kKinds; ++k) { t_n[k] = 0; } t_priced = 0; }
}
