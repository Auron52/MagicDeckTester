// Anti-Lifegain pump-then-Swords: whether a free-alt Invigorate is redirected onto the Swords to
// Plowshares target is a PROVIDER decision (DecisionProvider::RedirectPumpOntoRemovalTarget), not an
// engine rule (USER HARD RULE 2026-09-30). Two arms on the SAME board that MUST differ: the default
// provider redirects (Invigorate leaves hand, the target gets +4/+4, the opponent "gains" 3 -> loses
// 3 under Tainted Remedy); a declining provider leaves the hand and the target untouched.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

namespace
{
void EnsureCardsLoadedSR()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

struct NoRedirectProvider : public GenericProvider
{
    bool RedirectPumpOntoRemovalTarget(const GameState&, int, int, const CardDefinition&) const override
    { return false; }
};

GameState SwordsBoard(const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.m_provider = prov;
    int num = 200;
    auto put = [&](const char* name, int who)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Permanent p;
        p.card             = d->card;
        p.card.m_number    = num++;
        p.controller_index = who;
        p.owner_index      = who;
        s.battlefield.push_back(p);
    };
    put("Forest", 0);
    put("Tainted Remedy", 0);
    put("Llanowar Elves", 1);   // the Swords target, index 2
    const CardDefinition* inv = CardDatabase::Instance().Lookup("Invigorate");
    REQUIRE(inv != nullptr);
    Card c = inv->card;
    c.m_number = num++;
    s.players[0].hand.push_back(c);
    return s;
}
}   // namespace

TEST_CASE("Swords redirect: the default provider pumps the Swords target, a declining one does not")
{
    EnsureCardsLoadedSR();
    const CardDefinition* swords = CardDatabase::Instance().Lookup("Swords to Plowshares");
    REQUIRE(swords != nullptr);

    GameState a = SwordsBoard(&DefaultProvider());
    const int life0 = a.players[1].life;
    TryPumpThenSwordsRedirect(a, 0, 2, *swords);
    CHECK(a.players[0].hand.empty());
    CHECK(a.battlefield[2].temp_power_bonus > 0);
    CHECK(a.players[1].life < life0);

    NoRedirectProvider no;
    GameState b = SwordsBoard(&no);
    TryPumpThenSwordsRedirect(b, 0, 2, *swords);
    CHECK(b.players[0].hand.size() == 1);
    CHECK(b.battlefield[2].temp_power_bonus == 0);
    CHECK(b.players[1].life == life0);
}
