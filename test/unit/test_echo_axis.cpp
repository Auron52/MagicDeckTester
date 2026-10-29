// THE SEARCHED ECHO AXIS (MTG_ECHO_AXIS, DEFAULT ON).
//
// Echo's pay-vs-decline (CR 702.29) resolves at the NEXT upkeep. Both worlds took it from the provider
// there (PayEchoToKeep) -- at the executor's real upkeep and at every simulated turn boundary, including
// the ones the search crosses with depth remaining: an in-window greedy pick. Now the plan of the turn
// before carries the answer (Plan::echo_choice -> GameState::scripted_echo), relative to the provider
// ("follow" / "invert" per obligation ordinal), and the provider only orders the two. These pin:
//   * the pin encoding;
//   * the rollout's upkeep and the executor's upkeep read the pin through one function (lockstep), and
//     both consume it;
//   * a main-1 plan that casts an echo creature is searched BOTH ways (a "follow" base and its "invert"
//     clone, identical otherwise), and the plan apply writes the pin;
//   * main 2 decides only the obligations that arrived since main 1 (the cover is extended, never
//     re-branched), and decides an uncovered one (a Goblin Lackey's combat put) even on its idle plan;
//   * MTG_ECHO_AXIS=0 emits nothing (the provider's pick everywhere).
// Zone cards are NAME-ONLY placeholders, the way real play builds them (DeckLoader::MakePlaceholder).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsEa()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhEa(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void PutEa(GameState& s, const std::string& name, int number, bool entered_this_turn = false)
{
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = entered_this_turn;
    s.battlefield.push_back(p);
}

// Turn 3: two untapped Mountains, a library of Mountains so the boundary's draw is a land.
GameState BoardEa()
{
    EnsureCardsEa();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    PutEa(s, "Mountain", 11);
    PutEa(s, "Mountain", 12);
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(PhEa("Mountain", 40 + k)); }
    return s;
}

int CountNamed(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

int TappedMountains(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == "Mountain" && p.tapped) { ++n; } }
    return n;
}

bool CastsNamed(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    {
        if ((a.kind == Action::Kind::CastFromHand || a.kind == Action::Kind::ActivateVial)
            && a.card_name.str() == name) { return true; }
    }
    return false;
}

struct ArmEa
{
    explicit ArmEa(int v) { heurarm::t_arm[heurarm::ECHO_AXIS] = static_cast<std::int8_t>(v); }
    ~ArmEa() { heurarm::t_arm[heurarm::ECHO_AXIS] = -1; }
};

}   // namespace

TEST_CASE("Echo pin: the packed cover and invert mask")
{
    CHECK(EchoPinCount(-1) == 0);
    CHECK_FALSE(EchoPinInverts(-1, 0));
    const int pin = EchoPinPack(2, 0b10);
    CHECK(EchoPinCount(pin) == 2);
    CHECK(EchoPinMask(pin) == 0b10);
    CHECK_FALSE(EchoPinInverts(pin, 0));   // covered, follow
    CHECK(EchoPinInverts(pin, 1));         // covered, invert
    CHECK_FALSE(EchoPinInverts(pin, 2));   // past the cover: the provider
    CHECK_FALSE(EchoPinInverts(EchoPinPack(1, 0b10), 1));   // a bit past the cover is inert
}

TEST_CASE("Echo: the rollout's upkeep follows the provider unpinned and inverts it where pinned")
{
    // The base provider DECLINES a self-replacing body (Mogg War Marshal) and PAYS Stingscourger.
    for (const int pin : { -1, EchoPinPack(1, 1) })
    {
        GameState s = BoardEa();
        PutEa(s, "Mogg War Marshal", 10, /*entered_this_turn=*/true);
        s.scripted_echo = static_cast<std::int16_t>(pin);
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
        CHECK(s.scripted_echo == -1);   // consumed by the upkeep
        if (pin < 0)
        {
            CHECK(CountNamed(s, "Mogg War Marshal") == 0);   // declined: sacrificed
            CHECK(TappedMountains(s) == 0);
        }
        else
        {
            CHECK(CountNamed(s, "Mogg War Marshal") == 1);   // inverted: paid {1}{R}
            CHECK(TappedMountains(s) == 2);
        }
    }
    {
        GameState s = BoardEa();
        PutEa(s, "Mountain", 13);
        PutEa(s, "Mountain", 14);
        PutEa(s, "Stingscourger", 10, true);
        s.scripted_echo = static_cast<std::int16_t>(EchoPinPack(1, 1));
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
        CHECK(CountNamed(s, "Stingscourger") == 0);   // the provider pays; the pin declines
        CHECK(TappedMountains(s) == 0);
    }
}

TEST_CASE("Echo: the executor's upkeep reads the same pin as the rollout (lockstep) and consumes it")
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    for (const int pin : { -1, EchoPinPack(1, 1) })
    {
        GameState ex = BoardEa();
        PutEa(ex, "Mogg War Marshal", 10, true);
        ex.scripted_echo = static_cast<std::int16_t>(pin);
        eng.ResolveEchoUpkeep(ex);
        CHECK(ex.scripted_echo == -1);
        CHECK(CountNamed(ex, "Mogg War Marshal") == (pin < 0 ? 0 : 1));
        CHECK(TappedMountains(ex) == (pin < 0 ? 0 : 2));
    }
    // Two obligations, the second inverted only: ordinals are battlefield order in both worlds.
    {
        GameState ex = BoardEa();
        PutEa(ex, "Mountain", 13);
        PutEa(ex, "Mountain", 14);
        PutEa(ex, "Mogg War Marshal", 10, true);
        PutEa(ex, "Stingscourger", 15, true);
        GameState ro = ex;
        ex.scripted_echo = static_cast<std::int16_t>(EchoPinPack(2, 0b10));
        ro.scripted_echo = ex.scripted_echo;
        eng.ResolveEchoUpkeep(ex);
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(ro));
        for (const GameState* g : { &ex, &ro })
        {
            CHECK(CountNamed(*g, "Mogg War Marshal") == 0);   // ordinal 0 follows: declined
            CHECK(CountNamed(*g, "Stingscourger") == 0);      // ordinal 1 inverted: declined
        }
    }
}

TEST_CASE("Echo axis: a main-1 plan that casts Mogg War Marshal is searched both ways")
{
    ArmEa arm(1);
    GameState s = BoardEa();
    s.players[0].hand.push_back(PhEa("Mogg War Marshal", 20));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<int> mwm_pins;
    int mwm_plans = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (CastsNamed(p, "Mogg War Marshal")) { ++mwm_plans; mwm_pins.insert(p.echo_choice); }
        else { CHECK(p.echo_choice == -1); }   // no obligation: nothing to decide
    }
    REQUIRE(mwm_plans >= 2);
    CHECK(mwm_pins == std::set<int>{ EchoPinPack(1, 0), EchoPinPack(1, 1) });

    // The apply writes the pin onto the state, for the next upkeep.
    for (const TurnSolver::Plan& p : plans)
    {
        if (!CastsNamed(p, "Mogg War Marshal") || p.echo_choice != EchoPinPack(1, 1)) { continue; }
        GameState ro = s;
        TurnSolver::ApplyPlan(ro, p, true);
        CHECK(ro.scripted_echo == EchoPinPack(1, 1));
        break;
    }
}

TEST_CASE("Echo axis: main 2 extends main 1's cover and decides only what arrived since")
{
    ArmEa arm(1);
    // Main 1 cast Mogg War Marshal and its plan covered it (inverted); main 2 can cast Stingscourger.
    {
        GameState s = BoardEa();
        PutEa(s, "Mogg War Marshal", 10, true);
        s.scripted_echo = static_cast<std::int16_t>(EchoPinPack(1, 1));
        s.players[0].hand.push_back(PhEa("Stingscourger", 21));
        std::set<int> sting_pins;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, false))
        {
            if (CastsNamed(p, "Stingscourger")) { sting_pins.insert(p.echo_choice); }
            else { CHECK(p.echo_choice == -1); }   // covered already: inherits main 1's pin
        }
        // Ordinal 0 keeps main 1's answer (inverted); ordinal 1 (the new cast) is searched both ways.
        CHECK(sting_pins == std::set<int>{ EchoPinPack(2, 0b01), EchoPinPack(2, 0b11) });
    }
    // An UNCOVERED obligation (a Goblin Lackey's combat put): every main-2 plan decides it.
    {
        GameState s = BoardEa();
        PutEa(s, "Mogg War Marshal", 10, true);
        s.players[0].hand.push_back(PhEa("Lightning Bolt", 22));
        std::set<int> pins;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, false)) { pins.insert(p.echo_choice); }
        CHECK(pins == std::set<int>{ EchoPinPack(1, 0), EchoPinPack(1, 1) });
    }
}

TEST_CASE("Echo axis: MTG_ECHO_AXIS=0 emits nothing (the provider's pick everywhere)")
{
    ArmEa arm(0);
    GameState s = BoardEa();
    s.players[0].hand.push_back(PhEa("Mogg War Marshal", 20));
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { CHECK(p.echo_choice == -1); }
    GameState m2 = BoardEa();
    PutEa(m2, "Mogg War Marshal", 10, true);
    m2.players[0].hand.push_back(PhEa("Lightning Bolt", 22));
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(m2, false)) { CHECK(p.echo_choice == -1); }
}

TEST_CASE("Echo boundary fan: an obligation the apply created is decided at the crossing; a covered one is not")
{
    ArmEa arm(1);
    // A Mogg War Marshal a continuation / Muxus reveal put this turn, no pin covering it: follow + invert.
    GameState s = BoardEa();
    PutEa(s, "Mogg War Marshal", 10, true);
    CHECK(TurnSolver::EchoBoundaryPinsForTest(s) == std::vector<int>{ EchoPinPack(1, 0), EchoPinPack(1, 1) });
    // Main 1 already covered it (inverted): nothing left to decide.
    s.scripted_echo = static_cast<std::int16_t>(EchoPinPack(1, 1));
    CHECK(TurnSolver::EchoBoundaryPinsForTest(s).empty());
    // A second obligation arrived after the cover: only ordinal 1 is fanned, ordinal 0 keeps its answer.
    PutEa(s, "Stingscourger", 15, true);
    CHECK(TurnSolver::EchoBoundaryPinsForTest(s) == std::vector<int>{ EchoPinPack(2, 0b01), EchoPinPack(2, 0b11) });
    // The lever off: nothing is fanned.
    ArmEa off(0);
    CHECK(TurnSolver::EchoBoundaryPinsForTest(s).empty());
}
