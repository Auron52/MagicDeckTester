// Varchild's War-Riders cumulative upkeep is a PROVIDER decision (DecisionProvider::
// PaysCumulativeUpkeep), not an engine rule. USER 2026-09-30: *"even Varchild's should be done with a
// provider heuristic in theory. At least this is a helpful way to design it for the future when we do
// 1v1."* Both arms run on the SAME board: the default provider must pay (the opponent gets one
// Survivor per age counter and the War-Riders stays), and a provider that declines must sacrifice it
// (CR 702.24a: the age counter goes on first, then pay-or-sacrifice). The pay arm is the control -- a
// decline test with no arm that pays could pass because nothing ran.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{
void EnsureCardsLoadedCU()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

struct DeclineUpkeepProvider : public GenericProvider
{
    bool PaysCumulativeUpkeep(const GameState&, int) const override { return false; }
};

GameState VarchildBoard(const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.m_provider = prov;
    const CardDefinition* d = CardDatabase::Instance().Lookup("Varchild's War-Riders");
    REQUIRE(d != nullptr);
    Permanent p;
    p.card             = d->card;
    p.card.m_number    = 100;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s;
}

int CountControlledBy(const GameState& s, int who)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == who) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("Cumulative upkeep: the default provider pays, a declining provider sacrifices")
{
    EnsureCardsLoadedCU();

    // Control arm: pay. Two upkeeps -> 1 then 2 Survivors for the opponent, War-Riders stays.
    GameState pay = VarchildBoard(&DefaultProvider());
    PerformUpkeepCumulativeGifts(pay);
    CHECK(CountControlledBy(pay, 0) == 1);
    CHECK(CountControlledBy(pay, 1) == 1);
    PerformUpkeepCumulativeGifts(pay);
    CHECK(CountControlledBy(pay, 1) == 3);
    CHECK(pay.battlefield.front().age_counters == 2);

    // Decline arm: sacrificed to OUR graveyard, the opponent gets nothing.
    DeclineUpkeepProvider decline;
    GameState dec = VarchildBoard(&decline);
    PerformUpkeepCumulativeGifts(dec);
    CHECK(CountControlledBy(dec, 0) == 0);
    CHECK(CountControlledBy(dec, 1) == 0);
    REQUIRE(dec.players[0].graveyard.size() == 1);
    CHECK(dec.players[0].graveyard.front().m_name.str() == "Varchild's War-Riders");
}
