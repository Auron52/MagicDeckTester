// LandfallElementals -- CROSS-CLUSTER seams (integration, 2026-10-19).
//
// The deck's cards were built by three parallel clusters (lfe-land: the landfall bodies; lfe-etb: Risen
// Reef / Flamekin Harbinger; lfe-fury: Smokebraider / Incandescent Soulstoke / Fury). Each cluster
// tested its own cards; these cases pin the interactions that only exist once all three are in one tree:
//   * a Soulstoke PUT reaches Mossborn Hydra's as-enters counter (else the put Hydra is a dead 0/0),
//   * a Soulstoke PUT is an Elemental entering for Risen Reef (its look resolves on the put),
//   * a land Risen Reef puts onto the battlefield is a landfall for Hellhound / Brushfire / Hydra,
//   * Smokebraider pays Risen Reef's {1}{G}{U} (two units, any combination, Elemental spell).

#include <doctest/doctest.h>

#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLfeI()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

int g_ni = 9100;

Card CardI(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = g_ni++;
    return c;
}

// A LandfallElementals-shaped state: an Elemental library (so Metallic Mimic's chosen type is
// Elemental), the land drop SPENT (Risen Reef's default for a land on top is then "battlefield").
GameState BoardI(const std::vector<std::string>& library_top_first)
{
    EnsureCardsLfeI();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (const std::string& n : library_top_first) { s.players[0].library.push_back(CardI(n)); }
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(CardI(k % 2 ? "Akoum Hellhound" : "Brushfire Elemental")); }
    return s;
}

int PutI(GameState& s, const std::string& name, bool sick = false)
{
    Permanent p;
    p.card              = CardI(name);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

int HandI(GameState& s, const std::string& name)
{
    Card c = CardI(name);
    s.players[0].hand.push_back(c);
    return c.m_number;
}

const Permanent* ByNumI(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

const Permanent* ByNameI(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

int PlusI(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

bool InHandI(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("LFE seam: a Soulstoke PUT Mossborn Hydra enters with its +1/+1 counter (2 beside Mimic), hasted")
{
    GameState s = BoardI({ "Smokebraider" });
    const int soul = PutI(s, "Incandescent Soulstoke");
    const int hydra = HandI(s, "Mossborn Hydra");
    REQUIRE(ApplyPutFromHand(s, 0, soul, "Mossborn Hydra"));
    const Permanent* h = ByNumI(s, hydra);
    REQUIRE(h != nullptr);
    CHECK(PlusI(*h) == 1);   // the as-enters replacement ran on the put path: not a dead 0/0
    CHECK(h->temp_haste);

    GameState t = BoardI({ "Smokebraider" });
    const int soul2 = PutI(t, "Incandescent Soulstoke");
    const int mimic = PutI(t, "Metallic Mimic");
    FireEtbWatchers(t, 0, static_cast<int>(t.battlefield.size()) - 1);   // Mimic chooses Elemental
    (void)mimic;
    const int hydra2 = HandI(t, "Mossborn Hydra");
    REQUIRE(ApplyPutFromHand(t, 0, soul2, "Mossborn Hydra"));
    const Permanent* h2 = ByNumI(t, hydra2);
    REQUIRE(h2 != nullptr);
    CHECK(PlusI(*h2) == 2);  // its own counter + Mimic's additional counter
}

TEST_CASE("LFE seam: a Soulstoke PUT is an Elemental entering for Risen Reef (nonland top -> hand)")
{
    GameState s = BoardI({ "Flamekin Harbinger" });
    const int soul = PutI(s, "Incandescent Soulstoke");
    PutI(s, "Risen Reef");
    HandI(s, "Akoum Hellhound");
    const std::size_t lib_before = s.players[0].library.size();
    REQUIRE(ApplyPutFromHand(s, 0, soul, "Akoum Hellhound"));
    CHECK(s.players[0].library.size() == lib_before - 1);
    CHECK(InHandI(s, "Flamekin Harbinger"));   // the Reef looked on the put and took the nonland
}

TEST_CASE("LFE seam: a land Risen Reef puts in is a LANDFALL -- Hellhound pumps, Hydra doubles")
{
    GameState s = BoardI({ "Mountain" });
    const int hound = PutI(s, "Akoum Hellhound");
    const int hydra = PutI(s, "Mossborn Hydra");
    s.battlefield.back().counters.Add(Counter::Type::PlusOnePlusOne, 1);
    PutI(s, "Risen Reef", /*sick=*/true);
    const int slot = static_cast<int>(s.battlefield.size()) - 1;
    FireEtbWatchers(s, 0, slot);
    FireOwnEtbTriggers(s, 0, slot, "");
    const Permanent* m = ByNameI(s, "Mountain");
    REQUIRE(m != nullptr);
    CHECK(m->tapped);
    const Permanent* hh = ByNumI(s, hound);
    REQUIRE(hh != nullptr);
    CHECK(hh->temp_power_bonus == 2);
    const Permanent* hy = ByNumI(s, hydra);
    REQUIRE(hy != nullptr);
    CHECK(PlusI(*hy) == 2);
}

TEST_CASE("LFE seam: Smokebraider alone pays Risen Reef's {1}{G}{U}")
{
    GameState s = BoardI({});
    PutI(s, "Smokebraider");
    PutI(s, "Forest");
    const CardDefinition* reef = CardDatabase::Instance().Lookup("Risen Reef");
    REQUIRE(reef != nullptr);
    SpellSubtypePayScope scope(&reef->card);
    ManaCost c; c.generic = 1; c.green = 1; c.blue = 1;
    GameState t = s;
    CHECK(TapForCostDirect(t, c, /*for_creature=*/true));
}
