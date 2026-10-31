// The PARTIAL domain widen (MTG_DOMAIN_WIDEN_PARTIAL) and the widener-aware payable order
// (MTG_DOMAIN_WIDEN_ORDER) -- docs/design/mana-completeness-oracle.md 13.5, the FiveColour
// "SubsetWidensDomainBy is all-or-nothing" row. FiveColour s1100001 gi0 T3: Breeding Pool, Birds,
// Steam Vents and Faeburrow Elder (domain {G,W}, so Faeburrow taps for 2: pool 5). Deathrite ({B/G})
// paid by Breeding Pool adds BLACK, so Faeburrow then taps {W}{G}{B}, and with Steam Vents' {U} and
// Birds' {R} Jared Carthalion's {W}{U}{B}{R}{G} is paid: 6 mana off a pool the un-widened model reads
// as 5. The plan was cut twice: by the odometer's total-mana bound (5 < 6) and by the all-or-nothing
// widen credit (the payoff counted as a widener, 6 > 5 to prepay). Then the executor kept Jared first
// and dropped Deathrite.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// The gi0 T3 board, land drop spent, `payoff` + Deathrite in hand.
GameState Board(const std::string& payoff)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Breeding Pool", 9);
    Put(s, "Birds of Paradise", 5);
    Put(s, "Steam Vents", 45);
    Put(s, "Faeburrow Elder", 17);
    Hand(s, payoff, 10);
    Hand(s, "Deathrite Shaman", 13);
    return s;
}

bool Offers(const std::vector<TurnSolver::Plan>& plans, const std::string& a, const std::string& b)
{
    for (const TurnSolver::Plan& p : plans)
    {
        bool ha = false, hb = false;
        int casts = 0;
        for (const Action& x : p.actions)
        {
            if (x.kind != Action::Kind::CastFromHand) { continue; }
            ++casts;
            if (x.card_name.str() == a) { ha = true; }
            if (x.card_name.str() == b) { hb = true; }
        }
        if (ha && hb && casts == 2) { return true; }
    }
    return false;
}

// Pins one heurarm slot for the scope (the per-job batch override the A/B uses).
struct ArmPin
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPin(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = prev; }
};

}   // namespace

TEST_CASE("Partial domain widen: {Jared Carthalion, Deathrite} is offered off a 5-mana board that widens to 6")
{
    EnsureCardsLoaded();
    const GameState s = Board("Jared Carthalion");
    CHECK(Offers(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "Jared Carthalion", "Deathrite Shaman"));

    // The lever off: the all-or-nothing credit refuses it, as before.
    ArmPin off(heurarm::DOMAIN_WIDEN_PARTIAL, false);
    CHECK_FALSE(Offers(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "Jared Carthalion", "Deathrite Shaman"));
}

TEST_CASE("Widener-aware payable order: Deathrite is hoisted ahead of the payoff it funds")
{
    EnsureCardsLoaded();
    const GameState s = Board("Jared Carthalion");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* plan = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (Offers({ p }, "Jared Carthalion", "Deathrite Shaman")) { plan = &p; break; }
    }
    REQUIRE(plan != nullptr);
    std::vector<int> order;
    int jared = -1, drs = -1;
    for (int i = 0; i < static_cast<int>(plan->actions.size()); ++i)
    {
        const Action& a = plan->actions[static_cast<std::size_t>(i)];
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Jared Carthalion") { jared = i; }
        if (a.card_name.str() == "Deathrite Shaman") { drs = i; }
    }
    REQUIRE(jared >= 0);
    REQUIRE(drs >= 0);
    order = { jared, drs };   // the payoff first: Faeburrow's un-widened 2 cannot finish Jared after it
    ApplyPayableCastOrder(s, plan->actions, order);
    CHECK(order == std::vector<int>{ drs, jared });

    std::vector<int> off_order = { jared, drs };
    ArmPin off(heurarm::DOMAIN_WIDEN_ORDER, false);
    ApplyPayableCastOrder(s, plan->actions, off_order);
    CHECK(off_order == std::vector<int>{ jared, drs });
}
