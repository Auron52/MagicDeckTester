// A BURN SPELL'S OWN SCRY IS SEARCHED (Magma Jet: "deals 2 damage to any target. Scry 2.").
//
// Before this, a DirectDamage spell's cast_scry resolved through ChooseTopDisposition with no pin -- a single
// in-window provider pick (GenericProvider::ScryKeepOnTop: keep every nonland, keep a land only below two lands in
// play) that the search never alternatived over. USER 2026-11-17 (docs/design/searched-scry-disposition.md):
// "It should be searched and typically overridden by a provider and yes, this should be engine-wide."
// Two Creature Burn references (s6 gi5, s8 gi7) were a turn slower than the USER for exactly this reason: the
// heuristic bottomed both Mountains with a 4-land turn ahead (s6), and kept Browbeat + Goblin Fireslinger on top
// with Keldon Champion stuck at three lands (s8). Neither line was findable at --depth 8 --budget-ms 0.
//
// Now every Magma Jet cast is twinned per alternative disposition (Action::scry_pin, the step-9d Loran's Escape
// mechanism), sized off the look COUNT only (no clairvoyance about the look), and the pin rides to BOTH worlds:
// the rollout (apply_one's DirectDamage branch) and the executor (EffectHandler::ResolveDirectDamage).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <set>
#include <string>
#include <vector>

namespace
{

const CardDefinition& DefCs(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardCs(const std::string& name, int number)
{
    Card c = DefCs(name).card;
    c.m_number = number;
    return c;
}

void ResolveStackCs(GameState& s)
{
    while (!s.stack.empty())
    {
        const StackEntry e = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(s, e, *d); }
    }
}

// Two untapped Mountains, the land drop used, Magma Jet in hand; the library's top two are NONLANDS (the generic
// heuristic keeps both), then Mountains.
GameState JetBoard()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.phase               = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 2; ++i)
    {
        Permanent p;
        p.card = CardCs("Mountain", 5101 + i);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    s.players[0].hand.push_back(CardCs("Magma Jet", 5110));
    s.players[0].library.push_back(CardCs("Browbeat", 5120));
    s.players[0].library.push_back(CardCs("Goblin Fireslinger", 5121));
    for (int i = 0; i < 8; ++i) { s.players[0].library.push_back(CardCs("Mountain", 5130 + i)); }
    return s;
}

}   // namespace

TEST_CASE("cast-scry search: Magma Jet's scry 2 is twinned per disposition, and both worlds realise the pin")
{
    const GameState initial = JetBoard();
    std::set<int> pins;
    int checked = 0;
    for (const auto& p : TurnSolver::EnumerateMainPlans(initial, true))
    {
        const Action* jet = nullptr;
        for (const Action& a : p.actions) { if (a.card_name.str() == "Magma Jet") { jet = &a; } }
        if (jet == nullptr) { continue; }
        pins.insert(jet->scry_pin);
        INFO("scry_pin ", static_cast<int>(jet->scry_pin));
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackCs);
        CHECK(roll.players[1].life == 18);
        CHECK(exec.players[1].life == 18);
        const auto& lib = roll.players[0].library;
        REQUIRE(lib.size() == exec.players[0].library.size());
        REQUIRE(lib.size() == initial.players[0].library.size());
        for (std::size_t i = 0; i < lib.size(); ++i) { CHECK(lib[i].m_number == exec.players[0].library[i].m_number); }
        if (jet->scry_pin < 0)
        {   // the provider's disposition (candidate 0): both nonlands kept on top, in look order
            CHECK(lib[0].m_number == 5120);
            CHECK(lib[1].m_number == 5121);
        }
        else
        {   // candidate 1 = the first OTHER disposition in EnumerateTopDispositions order: all to the bottom
            CHECK(lib[0].m_number == 5130);
            CHECK(lib[lib.size() - 2].m_number == 5120);
            CHECK(lib[lib.size() - 1].m_number == 5121);
        }
        ++checked;
    }
    CHECK(pins.count(-1) == 1);
    CHECK(pins.count(1) == 1);
    CHECK(checked >= 2);
}

TEST_CASE("cast-scry search: an empty library emits no twin (nothing to look at)")
{
    GameState s = JetBoard();
    s.players[0].library = Library{};
    std::set<int> pins;
    for (const auto& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions) { if (a.card_name.str() == "Magma Jet") { pins.insert(a.scry_pin); } }
    }
    CHECK(pins == std::set<int>{ -1 });
}
