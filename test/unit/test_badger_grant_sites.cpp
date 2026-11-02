// Brightcap Badger's grant -- "Each Fungus and Saproling you control has '{T}: Add {G}.'" -- reaches
// two populations (ManaDefOf, SpellEffects.h): definition-less Saproling TOKENS, and real Fungus /
// Saproling cards with no mana ability of their own (Thallid, Sporecrown Thallid, Shroofus
// Sproutsire, Utopia Mycon). docs/design/badger-grant-defined-fungi-backtracker-gap.md: the greedy
// payer reaches both, the tap backtracker's source list only the first. These tests pin each payer
// on the second population.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/ManaPayment.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
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

int Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    p.def_absent        = false;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// Forest (#1) + a Fungus card with no mana ability (#10), with or without a Badger (#20).
GameState Board(const std::string& fungus, bool badger)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    Put(s, "Forest", 1);
    Put(s, fungus, 10);
    if (badger) { Put(s, "Brightcap Badger", 20); }
    return s;
}

ManaCost GG() { ManaCost c; c.green = 2; return c; }

}   // namespace

TEST_CASE("Brightcap Badger: a granted Fungus CARD pays through every payer")
{
    EnsureCardsLoaded();
    for (const char* fungus : { "Thallid", "Sporecrown Thallid", "Shroofus Sproutsire" })
    {
        CAPTURE(fungus);
        // The control: no Badger, {G}{G} off one Forest is refused by both payers.
        {
            GameState s = Board(fungus, false);
            CHECK(AvailableManaPool(s).Total() == 1);
            CHECK_FALSE(TapForCostBacktrack(s, GG(), false, ManaPool{}));
        }
        // The pool and the greedy payer see the granted {G}.
        {
            GameState s = Board(fungus, true);
            CHECK(AvailableManaPool(s).Total() == 2);
            CHECK(TapForCostShared(s, GG(), false, nullptr, false));
            CHECK(s.battlefield[1].tapped);
        }
        // The tap BACKTRACKER sees it too.
        {
            GameState s = Board(fungus, true);
            CHECK(TapForCostBacktrack(s, GG(), false, ManaPool{}));
            CHECK(s.battlefield[1].tapped);
        }
    }
}

namespace
{

// A 1/1 green Saproling TOKEN (no CardDefinition), as CreateTokenOnce makes it.
int PutSaproling(GameState& s, int number, bool sick = false)
{
    Permanent p;
    p.card.m_name       = "1/1 Saproling Token";
    p.card.RehashName();
    p.card.AddColor(Color::Green);
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes   = { "Saproling" };
    p.card.m_power      = 1;
    p.card.m_toughness  = 1;
    p.card.m_number     = number;
    p.is_token          = true;
    p.def_absent        = true;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

}   // namespace

TEST_CASE("Brightcap Badger: the same-plan grant credit counts the bodies a Badger cast unlocks")
{
    EnsureCardsLoaded();
    const CardDefinition& badger = Def("Brightcap Badger");
    GameState s = Board("Thallid", false);   // Forest, Thallid
    PutSaproling(s, 1000);
    PutSaproling(s, 1001, /*sick=*/true);   // summoning-sick: cannot use the granted {T}
    Put(s, "Undercellar Myconid", 30);       // a Fungus that is a mana dork already: in the pool
    s.battlefield.back().tapped = false;
    const ManaPool c = SamePlanGrantCredit(s, 0, badger);
    CHECK(c.green == 2);                      // the Thallid + the unsick Saproling
    CHECK(c.Total() == 2);

    // A tapped body adds nothing.
    s.battlefield[1].tapped = true;
    CHECK(SamePlanGrantCredit(s, 0, badger).Total() == 1);

    // A grant ALREADY live: a second Badger grants the same single ability.
    Put(s, "Brightcap Badger", 20);
    CHECK(SamePlanGrantCredit(s, 0, badger).Total() == 0);

    // A card that grants nothing earns nothing.
    CHECK(SamePlanGrantCredit(s, 0, Def("Forest")).Total() == 0);
}
