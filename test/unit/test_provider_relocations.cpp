// Engine-resident picks relocated behind provider hooks (USER HARD RULE 2026-09-30: heuristics live
// in providers). Each test has two arms on the SAME state that MUST differ -- the default provider
// vs an overriding one -- so it cannot pass because the engine ignored the hook.
//   * DecisionProvider::TopDispositionPick   -- the autonomous scry disposition (ScryTop)
//   * DecisionProvider::ReviveCandidates     -- Reveillark-style return order (no engine fallback)
//   * DecisionProvider::EtbDestroyVictimClass -- Terastodon's own-permanent victim order
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsLoadedPR()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card MakeCard(const char* name, int num)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = num;
    return c;
}

Permanent MakePerm(const char* name, int num, int who)
{
    Permanent p;
    p.card = MakeCard(name, num);
    p.controller_index = who;
    p.owner_index = who;
    return p;
}

struct KeepAllOnTop : public GenericProvider
{
    TopDisposition TopDispositionPick(const GameState&, const std::vector<Card>& looked, LookKind,
                                      int) const override
    {
        TopDisposition d;
        for (int i = 0; i < static_cast<int>(looked.size()); ++i) { d.top_order.push_back(i); }
        return d;
    }
};
struct BottomAll : public GenericProvider
{
    TopDisposition TopDispositionPick(const GameState&, const std::vector<Card>&, LookKind,
                                      int) const override
    { return TopDisposition{}; }
};

struct ReviveElvesOnly : public GenericProvider
{
    std::vector<std::string> ReviveCandidates(const GameState&, int, int, int) const override
    { return { "Llanowar Elves" }; }
};

struct ForestsLast : public GenericProvider
{
    int EtbDestroyVictimClass(const GameState& s, int controller, const Permanent& q) const override
    {
        const int base = GenericProvider::EtbDestroyVictimClass(s, controller, q);
        return base == 0 ? 4 : base;   // Forests move to the back of the line
    }
    int EtbDestroyK(const GameState&, int, const CardDefinition&) const override { return 1; }
};
struct DefaultOrderK1 : public GenericProvider
{
    int EtbDestroyK(const GameState&, int, const CardDefinition&) const override { return 1; }
};
}   // namespace

TEST_CASE("TopDispositionPick: ScryTop follows the provider's disposition")
{
    EnsureCardsLoadedPR();
    auto run = [](const DecisionProvider* prov)
    {
        GameState s;
        s.active_player_index = 0;
        s.m_provider = prov;
        s.players[0].library.push_back(MakeCard("Forest", 1));
        s.players[0].library.push_back(MakeCard("Llanowar Elves", 2));
        ScryTop(s, 1, "test");
        return s.players[0].library.front().m_number;
    };
    KeepAllOnTop keep;
    BottomAll bottom;
    CHECK(run(&keep) == 1);     // Forest stays on top
    CHECK(run(&bottom) == 2);   // Forest went to the bottom
}

TEST_CASE("ReviveCandidates: the provider's list decides; the default ranks by mana value")
{
    EnsureCardsLoadedPR();
    auto run = [](const DecisionProvider* prov)
    {
        GameState s;
        s.active_player_index = 0;
        s.m_provider = prov;
        s.players[0].graveyard = { MakeCard("Llanowar Elves", 10), MakeCard("Terastodon", 11) };
        PerformReturnFromGraveyardToBattlefield(s, 0, 1, 99, "test");
        return s.battlefield.empty() ? std::string() : s.battlefield.front().card.m_name.str();
    };
    CHECK(run(&DefaultProvider()) == "Terastodon");   // default: highest mana value first
    ReviveElvesOnly elves;
    CHECK(run(&elves) == "Llanowar Elves");
}

TEST_CASE("EtbDestroyVictimClass: Terastodon eats in the provider's order")
{
    EnsureCardsLoadedPR();
    auto run = [](const DecisionProvider* prov)
    {
        GameState s;
        s.active_player_index = 0;
        s.m_provider = prov;
        s.battlefield = { MakePerm("Forest", 20, 0), MakePerm("Sol Ring", 21, 0),
                          MakePerm("Terastodon", 22, 0) };
        FireOwnEtbTriggers(s, 0, 2, std::string(), kEtbKxHeuristic);
        REQUIRE(s.players[0].graveyard.size() == 1);
        return s.players[0].graveyard.front().m_name.str();
    };
    DefaultOrderK1 dflt;
    ForestsLast fl;
    CHECK(run(&dflt) == "Forest");
    CHECK(run(&fl) == "Sol Ring");
}
