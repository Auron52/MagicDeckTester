// Same-subset mana rocks that fund each other (MTG_ROCK_SEQ_CREDIT, DEFAULT ON).
//
// Both subset walkers credit a mana rock cast in the plan (Action::rock_mana) "only once the board can
// already pay for the rocks themselves" -- and tested that all-or-nothing, against the SUM of every
// selected rock's cost. So a rock that pays for another rock earned nothing, and the plan read unpayable:
// one Island, Sol Ring and Izzet Signet in hand -- the Island casts Sol Ring, whose {C}{C} casts the
// Signet -- was kept off the menu (NivMizzetCombo seed 1100014 gi13 T3, mana-oracle ENUM-GAP). The
// sequenced credit (SequencedRockProduction) credits a rock once the board plus the rocks credited before
// it pay for it. These pin: the pair is on the menu and both rocks resolve; the credit walk itself (a rock
// funded by the rocks credited before it, never by its own production); and a rock the board cannot fund
// even sequentially still earns nothing.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsRs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefRs(const std::string& name)
{
    EnsureCardsRs();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutRs(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefRs(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void HandRs(GameState& s, const std::string& name, int number)
{
    Card c = DefRs(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// `salt` keeps the boards of different arms distinct, so no cache serves one arm's menu to the other.
GameState RockBoard(int salt)
{
    EnsureCardsRs();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3 + salt;
    s.game_seed           = 7171 + salt;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefRs("Lightning Bolt").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    PutRs(s, "Island", 10);
    HandRs(s, "Sol Ring", 20);
    HandRs(s, "Izzet Signet", 21);
    return s;
}

bool CastsBoth(const TurnSolver::Plan& p)
{
    bool ring = false, signet = false;
    for (const Action& a : p.actions)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Sol Ring")     { ring = true; }
        if (a.card_name.str() == "Izzet Signet") { signet = true; }
    }
    return ring && signet;
}

int CountOnBoard(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

struct ArmRs
{
    heurarm::Slot slot;
    ArmRs(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmRs() { heurarm::t_arm[slot] = -1; }
};

}   // namespace

TEST_CASE("Rock sequencing: Sol Ring pays for the Izzet Signet in the same plan")
{
    ArmRs arm(heurarm::ROCK_SEQ_CREDIT, true);
    const GameState s = RockBoard(0);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = nullptr;
    for (const TurnSolver::Plan& p : plans) { if (CastsBoth(p)) { both = &p; break; } }
    REQUIRE(both != nullptr);

    GameState r = s;
    TurnSolver::ApplyPlan(r, *both, /*is_pre_combat=*/true);
    CHECK(CountOnBoard(r, "Sol Ring") == 1);
    CHECK(CountOnBoard(r, "Izzet Signet") == 1);
}

TEST_CASE("Rock sequencing: the credit walk funds a rock off the rocks credited before it")
{
    EnsureCardsRs();
    auto rock = [](const std::string& name, ManaPool out)
    {
        Action a;
        a.kind      = Action::Kind::CastFromHand;
        a.card_name = name;
        a.cost      = DefRs(name).card.m_mana_cost;
        a.rock_mana = out;
        return a;
    };
    ManaPool cc;  cc.colorless = 2;   // Sol Ring
    ManaPool ur;  ur.blue = 1; ur.red = 1;   // a Signet's {U}{R}, as a stand-in production
    const std::vector<Action> cands = { rock("Izzet Signet", ur), rock("Sol Ring", cc) };
    const std::vector<int> both = { 0, 1 };
    ManaPool one_land;  one_land.blue = 1;

    // The board's one mana pays only the Sol Ring; its {C}{C} then pays the Signet ({2}): both credited,
    // in whichever order they are selected. The all-or-nothing guard asked the board for {3} at once.
    CHECK(TurnSolver::SequencedRockProductionForTest(one_land, cands, both).Total() == 4);
    // The Signet alone is not fundable off one mana, so it earns nothing.
    CHECK(TurnSolver::SequencedRockProductionForTest(one_land, cands, { 0 }).Total() == 0);
    // No mana at all: nothing is fundable, so nothing is credited (a rock never funds its own cost).
    CHECK(TurnSolver::SequencedRockProductionForTest(ManaPool{}, cands, both).Total() == 0);
}

TEST_CASE("Rock sequencing: a rock the board cannot fund even in sequence earns nothing")
{
    ArmRs arm(heurarm::ROCK_SEQ_CREDIT, true);
    GameState s = RockBoard(2);
    s.battlefield.clear();   // no land: Sol Ring itself is unpayable, so nothing funds the Signet
    bool offered = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { if (CastsBoth(p)) { offered = true; } }
    CHECK_FALSE(offered);
}
