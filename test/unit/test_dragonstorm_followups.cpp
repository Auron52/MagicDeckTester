// Unit cover for the defects found by the USER-order audit (docs/design/dragonstorm-user-cast-order.md,
// "Found, not fixed" -> fixed in the 2026-10-10 follow-up round). One TEST_CASE group per lever.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsFu()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFu(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutFu(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefFu(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card CardFu(const std::string& name, int number)
{
    Card c     = DefFu(name).card;
    c.m_number = number;
    return c;
}

const DragonstormProvider& DsProvFu()
{
    static const DragonstormProvider p;
    return p;
}

// One lever forced for this thread, restored on scope exit.
struct ArmFu
{
    heurarm::Slot slot;
    ArmFu(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmFu() { heurarm::t_arm[slot] = -1; }
};

GameState BoardFu(const std::vector<std::string>& lands, const std::vector<std::string>& hand, int turn = 5)
{
    EnsureCardsFu();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &DsProvFu();
    int num = 300;
    for (const std::string& l : lands) { PutFu(s, l, num++); }
    num = 10;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardFu(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardFu("Mountain", 900 + k)); }
    return s;
}

bool CastsBoth(const TurnSolver::Plan& p, const std::string& a, const std::string& b)
{
    bool ha = false, hb = false;
    for (const Action& x : p.actions)
    {
        if (x.kind != Action::Kind::CastFromHand) { continue; }
        if (x.card_name.str() == a) { ha = true; }
        if (x.card_name.str() == b) { hb = true; }
    }
    return ha && hb;
}

}   // namespace

TEST_CASE("Ritual float colour: Irencrag's seven red cannot pay Karrthus's {B}{G} (MTG_RITUAL_FLOAT_COLOR_EXACT)")
{
    // Four Mountains + one City of Brass: Irencrag Feat (4) leaves one land and floats seven RED. Karrthus
    // {4}{B}{R}{G} needs a black AND a green pip; only the City makes either -> no order pays it. The wild
    // credit admitted the pair.
    const GameState s = BoardFu({ "Mountain", "Mountain", "Mountain", "Mountain", "City of Brass" },
                                { "Irencrag Feat", "Karrthus, Tyrant of Jund" });
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::RITUAL_FLOAT_COLOR_EXACT, on);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        bool any = false;
        for (const TurnSolver::Plan& p : plans) { if (CastsBoth(p, "Irencrag Feat", "Karrthus, Tyrant of Jund")) { any = true; } }
        CHECK(any == !on);
    }
    // With a SECOND City of Brass the same pair IS payable, lever on (the Mountains pay Irencrag, the float
    // pays Karrthus's red and generic, the two Cities its {B}{G}).
    const GameState s2 = BoardFu({ "Mountain", "Mountain", "Mountain", "Mountain", "City of Brass", "City of Brass" },
                                 { "Irencrag Feat", "Karrthus, Tyrant of Jund" });
    ArmFu arm(heurarm::RITUAL_FLOAT_COLOR_EXACT, true);
    bool any2 = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s2, true))
    { if (CastsBoth(p, "Irencrag Feat", "Karrthus, Tyrant of Jund")) { any2 = true; } }
    CHECK(any2);
}
