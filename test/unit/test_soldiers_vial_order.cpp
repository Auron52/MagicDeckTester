// Vial-put ORDER for the Soldiers entering classes. USER 2026-10-06: "Aether vial deployments should
// be done in the same order as casting" / "it should be done in the order the user specified" / "I
// should not be asked for an order. The order is apparent." So there is no before/after-the-casts
// axis any more: a Vial put is SEQUENCED LIKE A CAST of its card -- at its vector position on the
// explicit route (the human's queued order / a searched ordering), at its card's CastOrderRank
// otherwise -- and the order the viewer is told (RealisedNonSacCastOrder) is the order the apply
// realises. These pin that on the two entering classes that make the order matter (Champion of the
// Parish counting Humans that enter after it; Thalia's Lieutenant's ETB / grow split).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/HeuristicDefaults.h"
#include "ai/TurnSolver.h"

#include <algorithm>
#include <map>
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

TurnSolver::Plan Line(const std::vector<std::pair<std::string, bool>>& seq, bool explicit_order)
{
    // seq: (card, is_vial_put) in VECTOR order.
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const auto& e : seq)
    {
        Action a;
        a.kind = e.second ? Action::Kind::ActivateVial : Action::Kind::CastFromHand;
        a.card_name = e.first;
        if (!e.second) { a.def = &DefSV(e.first); }
        plan.actions.push_back(a);
    }
    plan.searched_order = explicit_order;
    return plan;
}

// How many of `others` come AFTER `card` in `order`.
int EnteringAfter(const std::vector<std::string>& order, const std::string& card,
                  const std::vector<std::string>& others)
{
    const auto at = std::find(order.begin(), order.end(), card);
    REQUIRE(at != order.end());
    int n = 0;
    for (auto it = at + 1; it != order.end(); ++it)
    { if (std::find(others.begin(), others.end(), *it) != others.end()) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Soldiers Vial order: on the explicit route a put resolves at its VECTOR position")
{
    // Hand Champion (10), Esper Sentinel (11), Cathar Commando (12, MV2 -- the only Vial-able card).
    for (bool put_last : {false, true})
    {
        CAPTURE(put_last);
        GameState s = SoldiersBoard(2, 2, {"Champion of the Parish", "Esper Sentinel", "Cathar Commando"});
        const TurnSolver::Plan plan = put_last
            ? Line({{"Champion of the Parish", false}, {"Esper Sentinel", false}, {"Cathar Commando", true}}, true)
            : Line({{"Cathar Commando", true}, {"Champion of the Parish", false}, {"Esper Sentinel", false}}, true);
        const std::vector<std::string> order = TurnSolver::RealisedNonSacCastOrder(s, plan);
        CHECK(order.size() == 3);   // the put is listed with the casts, under its creature's name
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        // Put first: only the Sentinel enters after the Champion (1). Put last: both do (2).
        CHECK(CountersOn(s, 10) == (put_last ? 2 : 1));
        CHECK(order.back() == (put_last ? "Cathar Commando" : "Esper Sentinel"));
    }
}

TEST_CASE("Soldiers Vial order: on the canonical route the put is ordered like a cast, and the wire says so")
{
    // Whatever the deck's order makes of the three, the apply must realise exactly the order
    // RealisedNonSacCastOrder reports -- so the line the human picks is the line that runs.
    for (bool put_last : {false, true})
    {
        CAPTURE(put_last);
        GameState s = SoldiersBoard(2, 2, {"Champion of the Parish", "Esper Sentinel", "Cathar Commando"});
        const TurnSolver::Plan plan = put_last
            ? Line({{"Champion of the Parish", false}, {"Esper Sentinel", false}, {"Cathar Commando", true}}, false)
            : Line({{"Cathar Commando", true}, {"Champion of the Parish", false}, {"Esper Sentinel", false}}, false);
        const std::vector<std::string> order = TurnSolver::RealisedNonSacCastOrder(s, plan);
        REQUIRE(order.size() == 3);
        TurnSolver::ApplyPlan(s, plan, true);
        CHECK(CountersOn(s, 10)
              == EnteringAfter(order, "Champion of the Parish", {"Esper Sentinel", "Cathar Commando"}));
    }
}

TEST_CASE("Soldiers Vial order: Thalia's Lieutenant's ETB/grow split follows the realised order")
{
    // Cast Lieutenant (10) + Vial-put Cathar Commando (11). Put first: Commando is on the battlefield
    // for the ETB (+1 on Commando). Put after: Lieutenant grows when Commando enters (+1 on itself).
    for (bool put_last : {false, true})
    {
        CAPTURE(put_last);
        GameState s = SoldiersBoard(2, 2, {"Thalia's Lieutenant", "Cathar Commando"});
        const TurnSolver::Plan plan = put_last
            ? Line({{"Thalia's Lieutenant", false}, {"Cathar Commando", true}}, true)
            : Line({{"Cathar Commando", true}, {"Thalia's Lieutenant", false}}, true);
        TurnSolver::ApplyPlan(s, plan, true);
        CHECK(CountersOn(s, 10) == (put_last ? 1 : 0));
        CHECK(CountersOn(s, 11) == (put_last ? 0 : 1));
    }
}

TEST_CASE("Soldiers Vial order: the enumerator offers ONE line per put/cast set -- no timing twin")
{
    GameState s = SoldiersBoard(2, 2, {"Champion of the Parish", "Esper Sentinel", "Cathar Commando"});
    std::map<std::string, int> seen;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.searched_order) { continue; }   // a searched cast ORDERING is a different line
        std::vector<std::string> k;
        for (const Action& a : p.actions)
        { k.push_back(std::to_string(static_cast<int>(a.kind)) + ":" + a.card_name.str()); }
        std::sort(k.begin(), k.end());
        std::string key = p.land_to_play;
        for (const std::string& e : k) { key += "|" + e; }
        CHECK_MESSAGE(++seen[key] == 1, "duplicate line ", key);
    }
}
