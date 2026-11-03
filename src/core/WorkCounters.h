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
    kKinds
};
inline const char* KindName(int k)
{
    static const char* n[kKinds] = { "step_c", "step_f", "greedy", "enum", "plans", "odo", "apply" };
    return (k >= 0 && k < kKinds) ? n[k] : "?";
}
inline thread_local long long t_n[kKinds] = {};
inline void Inc(int k, long long n = 1) { t_n[k] += n; }
inline void BeginGame() { for (int k = 0; k < kKinds; ++k) { t_n[k] = 0; } }
}
