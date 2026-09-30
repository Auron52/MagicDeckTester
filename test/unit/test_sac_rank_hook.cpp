// The sacrifice-victim rank is a PROVIDER heuristic (DecisionProvider::SacExpendabilityRank), not an
// engine rule (USER HARD RULE 2026-09-30): CanonicalSacVictim -- the one victim a sac-outlet action
// carries, and the multi-sac burst's picks -- must follow the provider. Two arms on the SAME board
// that MUST differ: the default rank sacrifices the TOKEN first; a provider that ranks tokens last
// sacrifices the card.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{
void EnsureCardsLoadedSRH()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

struct TokensLastProvider : public GenericProvider
{
    int SacExpendabilityRank(const GameState&, const Permanent& v, int, const std::vector<int>*) const override
    { return v.is_token ? 1000 : 0; }
};

GameState TwoBodies(const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.m_provider = prov;
    const CardDefinition* d = CardDatabase::Instance().Lookup("Llanowar Elves");
    REQUIRE(d != nullptr);
    Permanent card;
    card.card = d->card; card.card.m_number = 300; card.controller_index = 0; card.owner_index = 0;
    Permanent tok = card;
    tok.card.m_number = 301; tok.is_token = true;
    s.battlefield.push_back(card);
    s.battlefield.push_back(tok);
    return s;
}
}   // namespace

TEST_CASE("Sac victim rank: the canonical victim follows the provider's rank")
{
    EnsureCardsLoadedSRH();
    GameState a = TwoBodies(&DefaultProvider());
    CHECK(CanonicalSacVictim(a, 0, /*source_id=*/999, std::string()) == 301);   // token first

    TokensLastProvider tl;
    GameState b = TwoBodies(&tl);
    CHECK(CanonicalSacVictim(b, 0, /*source_id=*/999, std::string()) == 300);   // card first
}
