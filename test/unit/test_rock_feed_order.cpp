// A same-plan ramp-filter rock's mana needs a FEED (MTG_RAMP_FILTER_FEED_ORDER, DEFAULT ON).
//
// Izzet Signet is "{1}, {T}: Add {U}{R}": it nets one mana, but only once something else pays its {1}.
// The order walks behind the payable-order decider (ExactOrderWalkImpl / FirstUnpayablePosImpl, via
// CreditCastOutput) credited a cast Signet's Action::rock_mana (+1 wild) UNCONDITIONALLY, right after
// its own payment. On two Islands with Sol Ring and the Signet in hand, the order "Signet, Sol Ring"
// spends both Islands on the Signet and then pays Sol Ring out of that phantom +1 -- so the walk read
// it as payable, the decider kept it, and both apply worlds cast the Signet and dropped the Sol Ring
// (NivMizzetCombo, mana-oracle APPLY-DROP-ORDER: "the order Sol Ring > Izzet Signet pays every action").
// The USER: "There is nothing wrong with Sol Ring going first or paying for the Signet."
// With the lever on, the walk holds a filter rock's credit until the pool has a unit to feed it, so the
// Signet-first order projects unpayable and the decider realises Sol Ring first. These pin: the walk's
// verdict both ways, that a Signet with a feeder left still credits at once, and that the plan realises
// both rocks (lever on) where it dropped the Sol Ring (lever off).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsFo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFo(const std::string& name)
{
    EnsureCardsFo();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutFo(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefFo(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void HandFo(GameState& s, const std::string& name, int number)
{
    Card c = DefFo(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// `islands` untapped Islands; the Signet is FIRST in hand, so plan order lists it first.
// `salt` keeps the boards of different arms distinct, so no cache serves one arm's menu to the other.
GameState FeedBoard(int islands, int salt)
{
    EnsureCardsFo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3 + salt;
    s.game_seed           = 9393 + salt;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefFo("Lightning Bolt").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    for (int k = 0; k < islands; ++k) { PutFo(s, "Island", 10 + k); }
    HandFo(s, "Izzet Signet", 20);
    HandFo(s, "Sol Ring", 21);
    return s;
}

Action RockAct(const std::string& name, ManaPool out)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefFo(name);
    a.cost      = DefFo(name).card.m_mana_cost;
    a.rock_mana = out;
    return a;
}

int CountOnBoard(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

const TurnSolver::Plan* BothRocks(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        bool ring = false, signet = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Sol Ring")     { ring = true; }
            if (a.card_name.str() == "Izzet Signet") { signet = true; }
        }
        if (ring && signet) { return &p; }
    }
    return nullptr;
}

struct ArmFo
{
    heurarm::Slot slot;
    ArmFo(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmFo() { heurarm::t_arm[slot] = -1; }
};

}   // namespace

TEST_CASE("Ramp-filter feed: the order walk does not let a Signet's unfed +1 pay the Sol Ring after it")
{
    ManaPool cc;     cc.colorless = 2;   // Sol Ring
    ManaPool net1;   net1.wild    = 1;   // a fed Signet's net +1, as AddSourceToPool stamps it
    const std::vector<Action> acts = { RockAct("Izzet Signet", net1), RockAct("Sol Ring", cc) };
    const std::vector<int> signet_first = { 0, 1 }, ring_first = { 1, 0 };

    const GameState two = FeedBoard(2, 0);
    {
        ArmFo arm(heurarm::RAMP_FILTER_FEED_ORDER, false);
        CHECK(ExactOrderFirstUnpayable(two, acts, signet_first) == -1);   // the phantom: reads payable
    }
    {
        ArmFo arm(heurarm::RAMP_FILTER_FEED_ORDER, true);
        CHECK(ExactOrderFirstUnpayable(two, acts, signet_first) == 1);    // Sol Ring has nothing left
        CHECK(ExactOrderFirstUnpayable(two, acts, ring_first) == -1);     // Island + {C}{C}; the Island feeds
    }
    // A third Island is left over after the Signet, so it feeds the Signet at once: no change.
    const GameState three = FeedBoard(3, 1);
    {
        ArmFo arm(heurarm::RAMP_FILTER_FEED_ORDER, true);
        CHECK(ExactOrderFirstUnpayable(three, acts, signet_first) == -1);
    }
}

TEST_CASE("Ramp-filter feed: the decider realises Sol Ring before the Signet it pays for")
{
    for (const bool on : { false, true })
    {
        ArmFo arm(heurarm::RAMP_FILTER_FEED_ORDER, on);
        const GameState s = FeedBoard(2, on ? 3 : 2);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        const TurnSolver::Plan* both = BothRocks(plans);
        REQUIRE(both != nullptr);   // admitted by the sequenced rock credit (MTG_ROCK_SEQ_CREDIT)

        std::vector<int> order;
        int signet = -1, ring = -1;
        for (int i = 0; i < static_cast<int>(both->actions.size()); ++i)
        {
            const Action& a = both->actions[static_cast<std::size_t>(i)];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Izzet Signet") { signet = i; }
            if (a.card_name.str() == "Sol Ring")     { ring = i; }
        }
        REQUIRE(signet >= 0);
        REQUIRE(ring >= 0);
        order = { signet, ring };
        ApplyPayableCastOrder(s, both->actions, order);
        CHECK(order.front() == (on ? ring : signet));

        GameState r = s;
        TurnSolver::ApplyPlan(r, *both, /*is_pre_combat=*/true);
        CHECK(CountOnBoard(r, "Izzet Signet") == 1);
        CHECK(CountOnBoard(r, "Sol Ring") == (on ? 1 : 0));   // lever off: the APPLY-DROP-ORDER
    }
}
