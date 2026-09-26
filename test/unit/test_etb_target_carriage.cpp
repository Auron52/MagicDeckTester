// Executor carriage of a SEARCHED ETB target on the stack entry (2026-09-26).
//
// Several ETBs ride the chosen_x axis: the search emits one cast variant per target (chosen_x = the
// target's m_number) plus a DECLINE (chosen_x = 0). The rollout passes a.chosen_x straight into
// FireOwnEtbTriggers; the executor carries it on StackEntry::chosen_x, and EffectHandler reads
// value_or(-1) -- where -1 means "a PUT: ask the provider". So a ZERO the executor drops is not a
// decline at all: it silently becomes the provider's pick, and the game plays a line the search never
// scored. These tests pin the carriage end to end through the real executor cast + resolution.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include "mtg_test_seam.h"

#include <string>

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

void Put(GameState& s, const std::string& name, int number, bool tapped)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
}

const Permanent* ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

// Cast `name` from hand through the REAL executor with the given searched chosen_x, then resolve the
// pushed entry through the real EffectHandler (the path GameEngine::ResolveStack takes).
void CastAndResolve(GameState& s, const std::string& name, int number, int chosen_x)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    AIEngine eng;
    ManaPool avail = AvailableManaPool(s);
    MtgTestSeam::CastSpellFromHand(eng, s, s.players[0].hand.back(), avail, chosen_x);
    REQUIRE(!s.stack.empty());
    const StackEntry entry = s.stack.back();
    s.stack.pop_back();
    EffectHandler::Resolve(s, entry, Def(name));
}

}   // namespace

TEST_CASE("Felidar Guardian: a SEARCHED decline (chosen_x 0) reaches resolution as a decline")
{
    EnsureCardsLoaded();
    static MeliraPodProvider prov;   // its FlickerTarget WOULD flicker a tapped land (rank 3)
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.m_provider = &prov;
    for (int n = 1; n <= 4; ++n) { Put(s, "Plains", n, /*tapped=*/false); }
    Put(s, "Plains", 9, /*tapped=*/true);    // a tapped land: the provider flickers one if the zero is dropped
    CHECK(prov.FlickerTarget(s, 0, 50) == 9);

    CastAndResolve(s, "Felidar Guardian", 50, /*chosen_x=*/0);
    REQUIRE(ByNumber(s, 50) != nullptr);     // Felidar resolved onto the battlefield
    // Declined: NO land came back as a new untapped object. (After paying, all five Plains are
    // tapped, so a dropped zero would have the provider flicker the first of them.)
    int untapped_lands = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && !p.tapped) { ++untapped_lands; } }
    CHECK(untapped_lands == 0);
}

TEST_CASE("Felidar Guardian: a searched target (chosen_x = m_number) is flickered")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    for (int n = 1; n <= 4; ++n) { Put(s, "Plains", n, /*tapped=*/false); }
    Put(s, "Plains", 9, /*tapped=*/true);
    CastAndResolve(s, "Felidar Guardian", 50, /*chosen_x=*/9);
    bool untapped_plains_9 = false;
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == "Plains" && p.card.m_number == 9 && !p.tapped) { untapped_plains_9 = true; } }
    CHECK(untapped_plains_9);                // flicker = a new, untapped object
}
