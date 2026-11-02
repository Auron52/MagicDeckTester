// Unit tests for Paradise Mantle -- "Equipped creature has '{T}: Add one mana of any color.'
// Equip {1}" (CardParams::equip_grants_tap_mana_any).
//
// The grant rides Brightcap Badger's chokepoint (LiveManaGrant / ManaDefOf / GrantFaceFor in
// SpellEffects.h): the HOST is handed a synthetic any-colour ManaDork face. The host is always a
// real card with its own definition and no mana ability (Puresteel, Sram, Adelbert...), which is
// the population two payment sites -- the tap backtracker's source list and ManaCacheKey -- used to
// skip for the Badger, so each payer and bound is asserted here rather than inferred.
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

int Put(GameState& s, const std::string& name, int number, bool sick = false, int equipped_to = 0)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    p.def_absent        = false;
    p.equipped_to       = equipped_to;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// One Plains + Puresteel Paladin (#10), optionally carrying a Mantle (#20).
GameState Board(bool mantle_attached, bool host_sick = false)
{
    GameState s = Fresh();
    Put(s, "Plains", 1);
    Put(s, "Puresteel Paladin", 10, host_sick);
    Put(s, "Paradise Mantle", 20, false, mantle_attached ? 10 : 0);
    return s;
}

int Tapped(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.tapped) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Paradise Mantle: the card is loaded with the grant param")
{
    EnsureCardsLoaded();
    const CardDefinition& d = Def("Paradise Mantle");
    CHECK(d.params.is_equipment);
    CHECK(d.params.equip_cost_generic == 1);
    CHECK(d.params.equip_grants_tap_mana_any);
    CHECK(d.card.HasType(CardType::Artifact));
}

TEST_CASE("Paradise Mantle: the equipped creature is a real any-colour mana source")
{
    EnsureCardsLoaded();

    SUBCASE("UNATTACHED, the Mantle grants nothing -- the control")
    {
        GameState s = Board(false);
        CHECK(AvailableManaPool(s).Total() == 1);
        CHECK(LooseManaCeiling(s, 0) == 1);
        ManaCost ww; ww.white = 2;
        CHECK_FALSE(TapForCostShared(s, ww, false, nullptr, false));
    }

    SUBCASE("ATTACHED, the host adds one -- through the pool, the ceiling and the bound")
    {
        GameState s = Board(true);
        CHECK(AvailableManaPool(s).Total() == 2);
        CHECK(LooseManaCeiling(s, 0) == 2);
        CHECK(UntappedManaUpperBound(s, false, 0) >= 2);
        CHECK(PaymentManaCovers(s, false, 2));
    }

    SUBCASE("and the payer SPENDS it: {W}{W} off one Plains taps Plains + the host")
    {
        GameState s = Board(true);
        ManaCost ww; ww.white = 2;
        CHECK(TapForCostShared(s, ww, false, nullptr, false));
        CHECK(Tapped(s) == 2);
        CHECK(s.battlefield[1].tapped);     // the host
        CHECK_FALSE(s.battlefield[2].tapped);   // the Mantle itself never taps
    }

    SUBCASE("ANY colour: a {U} pip is payable off the host alone")
    {
        GameState s = Board(true);
        ManaCost u; u.blue = 1;
        CHECK(TapForCostShared(s, u, false, nullptr, false));
        CHECK(s.battlefield[1].tapped);
        CHECK_FALSE(s.battlefield[0].tapped);   // the Plains cannot make blue
    }

    SUBCASE("a cost one larger than the board is REFUSED")
    {
        GameState s = Board(true);
        ManaCost c; c.generic = 2; c.white = 1;
        CHECK_FALSE(TapForCostShared(s, c, false, nullptr, false));
    }

    SUBCASE("SUMMONING SICKNESS holds -- CR 302.6 applies to the granted {T}")
    {
        GameState s = Board(true, /*host_sick=*/true);
        CHECK(AvailableManaPool(s).Total() == 1);
        ManaCost ww; ww.white = 2;
        CHECK_FALSE(TapForCostShared(s, ww, false, nullptr, false));
    }

    SUBCASE("...and equip haste lifts it (Skateboard on the same host)")
    {
        GameState s = Board(true, /*host_sick=*/true);
        Put(s, "Skateboard", 30, false, /*equipped_to=*/10);
        CHECK(AvailableManaPool(s).Total() == 2);
        ManaCost ww; ww.white = 2;
        CHECK(TapForCostShared(s, ww, false, nullptr, false));
    }

    SUBCASE("a TAPPED host makes nothing")
    {
        GameState s = Board(true);
        s.battlefield[1].tapped = true;
        CHECK(AvailableManaPool(s).Total() == 1);
    }

    SUBCASE("TWO Mantles on one host grant ONE ability -- it can only tap once")
    {
        GameState s = Board(true);
        Put(s, "Paradise Mantle", 21, false, /*equipped_to=*/10);
        CHECK(AvailableManaPool(s).Total() == 2);
        CHECK(LooseManaCeiling(s, 0) == 2);
    }

    SUBCASE("two Mantles on two hosts are two sources")
    {
        GameState s = Board(true);
        Put(s, "Sram, Senior Edificer", 11);
        Put(s, "Paradise Mantle", 21, false, /*equipped_to=*/11);
        CHECK(AvailableManaPool(s).Total() == 3);
        ManaCost c; c.white = 3;
        CHECK(TapForCostShared(s, c, false, nullptr, false));
        CHECK(Tapped(s) == 3);
    }

    SUBCASE("THE CONTAINMENT PROPERTY: a deck stamped without a grant pays nothing for it")
    {
        GameState s = Board(true);
        s.deck_has_mana_grant       = false;
        s.deck_has_equip_mana_grant = false;
        CHECK(AvailableManaPool(s).Total() == 1);
    }
}
