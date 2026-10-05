// Vial-put ORDER for the Soldiers entering classes (Soldiers 5d claude-play sweep, seed 9100047,
// 2026-10-04). Plan::vial_after_casts used to be emitted only for a param list (Metallic Mimic,
// Forerunner of the Coalition, Daring Buccaneer, a Vial-put Thalia), so "cast Champion of the Parish,
// THEN Vial-put a Human" -- one more +1/+1 counter -- was offered to neither the search nor the human.
// The gate is now MEASURED (TurnSolver::VialOrderChangesOutcome: apply both orders, diff the
// canonical position), and these pin both halves: the apply realises each order, and the enumerator
// offers the twin exactly where the two orders reach different positions.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/HeuristicDefaults.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsSV()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSV(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSV(const std::string& name, int number)
{
    Card c = DefSV(name).card;
    c.m_number = number;
    return c;
}

void PutSV(GameState& s, const std::string& name, int number, int charge = 0)
{
    Permanent p;
    p.card             = CardSV(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.charge_counters  = charge;
    s.battlefield.push_back(p);
}

int CountersOn(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_number != number) { continue; }
        int n = 0;
        for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
        return n;
    }
    FAIL("permanent not on the battlefield: ", number);
    return -1;
}

// Turn 3, pre-combat: Aether Vial on `charge`, `plains` untapped Plains, the given hand (numbered
// 10, 11, ... in order), a dead library.
GameState SoldiersBoard(int charge, int plains, const std::vector<std::string>& hand)
{
    EnsureCardsSV();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    PutSV(s, "Aether Vial", 1, charge);
    for (int k = 0; k < plains; ++k) { PutSV(s, "Plains", 2 + k); }
    int num = 10;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardSV(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardSV("Plains", 900 + k)); }
    return s;
}

TurnSolver::Plan CastsThenVial(const std::vector<std::string>& casts, const std::string& vial, bool after)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const std::string& n : casts)
    {
        Action c;
        c.kind = Action::Kind::CastFromHand; c.card_name = n; c.def = &DefSV(n);
        plan.actions.push_back(c);
    }
    Action v;
    v.kind = Action::Kind::ActivateVial; v.card_name = vial;
    plan.actions.push_back(v);
    plan.vial_after_casts = after;
    return plan;
}

}   // namespace

TEST_CASE("Soldiers Vial order: cast Champion of the Parish, THEN Vial-put a Human -> two counters")
{
    // Hand Champion (10), Esper Sentinel (11), Cathar Commando (12, MV2 -- the only Vial-able card).
    for (bool after : {false, true})
    {
        CAPTURE(after);
        GameState s = SoldiersBoard(2, 2, {"Champion of the Parish", "Esper Sentinel", "Cathar Commando"});
        const TurnSolver::Plan plan =
            CastsThenVial({"Champion of the Parish", "Esper Sentinel"}, "Cathar Commando", after);
        CHECK(TurnSolver::VialOrderMatters(plan));
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        // Puts-first: Commando enters before the Champion -> only the Sentinel counts (1).
        // Puts-last: Sentinel AND Commando enter after it (2).
        CHECK(CountersOn(s, 10) == (after ? 2 : 1));
    }
}

TEST_CASE("Soldiers Vial order: the enumerator offers the puts-last twin, and it reaches the 2-counter Champion")
{
    GameState s = SoldiersBoard(2, 2, {"Champion of the Parish", "Esper Sentinel", "Cathar Commando"});
    int twins = 0, best = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.vial_after_casts) { ++twins; CHECK(TurnSolver::VialOrderMatters(p)); }
        GameState c = s;
        TurnSolver::ApplyPlan(c, p, true);
        for (const Permanent& q : c.battlefield)
        { if (q.card.m_number == 10) { best = std::max(best, CountersOn(c, 10)); } }
    }
    CHECK(twins > 0);
    CHECK(best == 2);   // was 1: no plan could put the Commando after the Champion
}

TEST_CASE("Soldiers Vial order: Thalia's Lieutenant's ETB/grow split is an order the search can choose")
{
    // Cast Lieutenant (10) + Vial-put Cathar Commando (11). Puts-first: Commando is on the battlefield
    // for the ETB (+1 on Commando). Puts-last: Lieutenant grows when Commando enters (+1 on itself).
    for (bool after : {false, true})
    {
        CAPTURE(after);
        GameState s = SoldiersBoard(2, 2, {"Thalia's Lieutenant", "Cathar Commando"});
        TurnSolver::ApplyPlan(s, CastsThenVial({"Thalia's Lieutenant"}, "Cathar Commando", after), true);
        CHECK(CountersOn(s, 10) == (after ? 1 : 0));
        CHECK(CountersOn(s, 11) == (after ? 0 : 1));
    }
    GameState s = SoldiersBoard(2, 2, {"Thalia's Lieutenant", "Cathar Commando"});
    int twins = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { twins += p.vial_after_casts ? 1 : 0; }
    CHECK(twins > 0);
}

TEST_CASE("Soldiers Vial order: no entering effect reads the other entries -> no twin")
{
    // Esper Sentinel cast + Cathar Commando Vial-put: neither card's entry depends on the other's, so
    // both orders reach the same position and the enumerator must not grow the candidate list.
    GameState s = SoldiersBoard(2, 1, {"Esper Sentinel", "Cathar Commando"});
    const TurnSolver::Plan plan = CastsThenVial({"Esper Sentinel"}, "Cathar Commando", false);
    CHECK(TurnSolver::VialOrderMatters(plan));                     // structurally orderable...
    CHECK_FALSE(TurnSolver::VialOrderChangesOutcome(s, plan, true));   // ...but the order is inert
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { CHECK_FALSE(p.vial_after_casts); }
}
