// MTG_LINE_REPAY -- the depth-1 rollback of a line's previous payment (ManaPayment.cpp LineRepayTry;
// docs/design/mana-completeness-oracle.md section 14). The mana-clean gate's Dragons pass (pass1-dragons, 71,000
// games) found the per-cast payer dropping a line's later cast because the cast before it spent the wrong
// source, on two shapes that both turn on Gruul Turf ({T}: add {R}{G}):
//
//  * APPLY-DROP: {Mind Stone, Scourge of Valkas} off Mountain, Mountain, Mind Stone, Gruul Turf with Dragonspeaker
//    Shaman out (Scourge {R}{R}{R}). The payer paid Mind Stone's {2} with a Mountain and the old Mind Stone; Gruul
//    Turf then makes only one {R}. The line pays when the Turf's {G} and the old Mind Stone pay the {2} and its {R}
//    floats into Scourge.
//  * ENUM-GAP: {Urza's Incubator, Lathliss, Dragon Queen} off Mountain, Mind Stone, Gruul Turf and a fresh Haven of
//    the Spirit Dragon with the Shaman out. Incubator ({3}) discounts Lathliss to {R}{R}; the payer paid the {3}
//    with the Mountain, the Haven's {C} and Mind Stone, so the enumerator's sequential walk (the same payer) found
//    Lathliss unpayable and kept the plan off the menu.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

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

void Put(GameState& s, const std::string& name, int number, bool entered_this_turn = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = entered_this_turn;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState BaseBoard()
{
    static const DragonsProvider prov;   // the deck's own cast order: rocks and reducers before the Dragons
    GameState s;
    s.m_provider          = &prov;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    // A Dragon library: Urza's Incubator names the deck's dominant creature type as it enters.
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def(k % 2 == 0 ? "Glorybringer" : "Mountain").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

struct ArmPin
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPin(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = prev; }
};

bool PlanCasts(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    for (const std::string& n : names)
    {
        bool found = false;
        for (const Action& x : p.actions)
        { if (x.kind == Action::Kind::CastFromHand && x.card_name.str() == n) { found = true; } }
        if (!found) { return false; }
    }
    return true;
}

bool OnBattlefield(const GameState& s, const std::string& name, int number)
{
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == name && p.card.m_number == number) { return true; } }
    return false;
}

// Mountain, Mountain (this turn's land), Mind Stone, Dragonspeaker Shaman, Gruul Turf; Scourge + Mind Stone in hand.
GameState ScourgeBoard()
{
    GameState s = BaseBoard();
    Put(s, "Mountain", 1);
    Put(s, "Mind Stone", 2);
    Put(s, "Dragonspeaker Shaman", 3);
    Put(s, "Gruul Turf", 4);
    Put(s, "Mountain", 5, /*entered_this_turn=*/true);
    Hand(s, "Scourge of Valkas", 10);
    Hand(s, "Mind Stone", 11);
    return s;
}

// The applied plan: does Scourge of Valkas reach the battlefield next to the new Mind Stone?
bool ScourgeLinePays(const GameState& s0)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s0, /*is_pre_combat=*/true))
    {
        if (p.actions.size() != 2 || !PlanCasts(p, { "Scourge of Valkas", "Mind Stone" })) { continue; }
        GameState s = s0;
        TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
        return OnBattlefield(s, "Scourge of Valkas", 10) && OnBattlefield(s, "Mind Stone", 11);
    }
    FAIL("the menu does not hold {Mind Stone, Scourge of Valkas}");
    return false;
}

// Mountain, Mind Stone, Dragonspeaker Shaman, Gruul Turf, a fresh Haven of the Spirit Dragon; Incubator + Lathliss.
GameState IncubatorBoard()
{
    GameState s = BaseBoard();
    Put(s, "Mountain", 1);
    Put(s, "Mind Stone", 2);
    Put(s, "Dragonspeaker Shaman", 3);
    Put(s, "Gruul Turf", 4);
    Put(s, "Haven of the Spirit Dragon", 5, /*entered_this_turn=*/true);
    Hand(s, "Urza's Incubator", 10);
    Hand(s, "Lathliss, Dragon Queen", 11);
    return s;
}

bool OffersIncubatorLathliss(const GameState& s)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (PlanCasts(p, { "Urza's Incubator", "Lathliss, Dragon Queen" })) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Line repay: {Mind Stone, Scourge of Valkas} pays both off Gruul Turf's {G} and its floating {R}")
{
    EnsureCardsLoaded();
    const GameState s = ScourgeBoard();
    {
        ArmPin on(heurarm::LINE_REPAY, true);
        CHECK(ScourgeLinePays(s));
    }
    // The lever off: the per-cast payer's Mind Stone payment strands the third {R} and Scourge is dropped.
    ArmPin off(heurarm::LINE_REPAY, false);
    CHECK_FALSE(ScourgeLinePays(s));
}

TEST_CASE("Line repay: {Urza's Incubator, Lathliss} is on the menu (the sequential walk shares the payer)")
{
    EnsureCardsLoaded();
    const GameState s = IncubatorBoard();
    {
        ArmPin on(heurarm::LINE_REPAY, true);
        CHECK(OffersIncubatorLathliss(s));
    }
    ArmPin off(heurarm::LINE_REPAY, false);
    CHECK_FALSE(OffersIncubatorLathliss(s));
}

TEST_CASE("Line repay: a line the board cannot pay by amount is left alone")
{
    EnsureCardsLoaded();
    // Without the second Mountain the board makes five mana against Mind Stone {2} + Scourge {R}{R}{R}: short by
    // one. No re-payment can conjure mana, so the plan stays dropped with the lever on.
    GameState s = ScourgeBoard();
    s.battlefield.pop_back();
    ArmPin on(heurarm::LINE_REPAY, true);
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.actions.size() != 2 || !PlanCasts(p, { "Scourge of Valkas", "Mind Stone" })) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        CHECK_FALSE((OnBattlefield(t, "Scourge of Valkas", 10) && OnBattlefield(t, "Mind Stone", 11)));
    }
}
