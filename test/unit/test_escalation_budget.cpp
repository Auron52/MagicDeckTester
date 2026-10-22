// The hybrid's escalation budget is a USER rule: 2026-09-11 "make the fresh escalation budget unconditional";
// 2026-10-21 "The escalated shape was meant to always use 0.5" (escalating on the probe's leftovers "should not be
// reachable"); 2026-10-22 "0.5 floor probably and 0.5 default certainly". Agents reverted it silently twice
// (2026-09-03, 2026-09-12), and every deck without a per-deck key then escalated on whatever the probe left --
// often nothing. These cases pin it. Change an expectation here only on the USER's word.
// docs/design/search-shape-open-items.md s3; TurnSolver::ChooseEscalationBudget.
//
// The default case reads the A/B env (MTG_ESCALATION_FRESH_FRAC / MTG_ESCALATION_FRESH_FLOOR): run the unit
// tests without them set.
#include <doctest/doctest.h>

#include "ai/SearchBudget.h"
#include "ai/TurnSolver.h"

TEST_CASE("escalation budget: the default is a fresh 0.5 x budget_ms, as a floor")
{
    const TurnSolver::EscalationBudgetPolicy p = TurnSolver::DefaultEscalationBudgetPolicy();
    CHECK(p.frac == doctest::Approx(0.5));
    CHECK(p.floor);
}

TEST_CASE("escalation budget: a probe that spent the budget still leaves the escalation a fresh half")
{
    const TurnSolver::EscalationBudgetPolicy p{0.5, true};
    const long long half = SearchBudget::FromVirtualMs(10).Limit();

    TurnSolver::EscalationBudgetChoice c = TurnSolver::ChooseEscalationBudget(0, 20, p);   // nothing left
    CHECK(c.fresh);
    CHECK(c.ms == 10);
    CHECK(c.units == half);

    c = TurnSolver::ChooseEscalationBudget(-500, 20, p);   // the probe overran
    CHECK(c.fresh);
    CHECK(c.units == half);

    c = TurnSolver::ChooseEscalationBudget(half / 2, 20, p);   // a leftover below the floor
    CHECK(c.fresh);
    CHECK(c.units == half);
}

TEST_CASE("escalation budget: under the floor a cheap probe's larger leftover is kept")
{
    const TurnSolver::EscalationBudgetPolicy p{0.5, true};
    const long long half = SearchBudget::FromVirtualMs(10).Limit();
    CHECK_FALSE(TurnSolver::ChooseEscalationBudget(half + 1, 20, p).fresh);
    CHECK(TurnSolver::ChooseEscalationBudget(half, 20, p).fresh);   // equal: the fresh half
}

TEST_CASE("escalation budget: the research arms and the unbudgeted search")
{
    // A flat frac ignores a larger leftover.
    CHECK(TurnSolver::ChooseEscalationBudget(1000000, 20, {0.5, false}).fresh);
    // frac -1 is the legacy leftover arm (A/B only); an unbudgeted search keeps its unlimited budget.
    CHECK_FALSE(TurnSolver::ChooseEscalationBudget(0, 20, {-1.0, true}).fresh);
    CHECK_FALSE(TurnSolver::ChooseEscalationBudget(-1, 0, {0.5, true}).fresh);
}
