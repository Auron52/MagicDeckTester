// Terastodon's destroy-K on an unpinned entry (kEtbKxHeuristic -- the autonomous cast and every PUT
// route) is a PROVIDER decision (DecisionProvider::EtbDestroyK), not an engine rule: the engine
// consults the hook at resolution and destroys exactly that many of our noncreature permanents,
// making one 3/3 Elephant each. USER HARD RULE 2026-09-30 (no heuristic substitute in the engine).
// Two arms on the SAME board that MUST differ (K=0 vs K=2), so the test cannot pass because nothing
// ran; the fixture test/scenarios/stompy_terastodon_k.json covers the default projection's play.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{
void EnsureCardsLoadedTK()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

struct FixedKProvider : public GenericProvider
{
    int k;
    explicit FixedKProvider(int kk) : k(kk) {}
    int EtbDestroyK(const GameState&, int, const CardDefinition&) const override { return k; }
};

GameState TerastodonBoard(const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.m_provider = prov;
    int num = 100;
    auto put = [&](const char* name)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Permanent p;
        p.card             = d->card;
        p.card.m_number    = num++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    };
    put("Forest"); put("Forest"); put("Forest");
    put("Terastodon");
    return s;
}

int Count(const GameState& s, bool lands)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (lands ? p.card.IsLand() : (p.card.IsCreature() && p.is_token)) { ++n; }
    }
    return n;
}
}   // namespace

TEST_CASE("Terastodon destroy-K: an unpinned entry destroys exactly the provider's K")
{
    EnsureCardsLoadedTK();

    FixedKProvider none(0);
    GameState a = TerastodonBoard(&none);
    FireOwnEtbTriggers(a, 0, 3, std::string(), kEtbKxHeuristic);
    CHECK(Count(a, true) == 3);
    CHECK(Count(a, false) == 0);

    FixedKProvider two(2);
    GameState b = TerastodonBoard(&two);
    FireOwnEtbTriggers(b, 0, 3, std::string(), kEtbKxHeuristic);
    CHECK(Count(b, true) == 1);
    CHECK(Count(b, false) == 2);
    CHECK(b.players[0].graveyard.size() == 2);
}
