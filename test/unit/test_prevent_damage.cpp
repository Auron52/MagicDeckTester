// Unit tests for the Prevent Damage onboarding, phase I1 (2026-09-27): the damage-event core in
// src/core/DamageEvents.h. Every number here is a RULES number read off the cards' Oracle text and
// rulings, pinned exactly -- the per-EVENT granularity is observable (Dina drains per event, Bilbo
// adds per event), so an aggregate that sums right but counts events wrong must fail here.
//
//  1. Tamanoa gains on a painland's coloured tap; nothing on its painless {C} tap.
//  2. Rhox Faithmender doubles every lifegain event; two multiply (x4); Bilbo's +1 goes FIRST.
//  3. Vito drains "that much" (the REPLACED amount); Dina drains 1 per EVENT.
//  4. Manabarbs: one damage event per land tap -> one Tamanoa trigger each -> one Dina drain each.
//  5. Lethal self-damage LOSES even with Tamanoa out (the SBA runs before the trigger resolves), and
//     the loss is sticky (a later gain cannot revive us).
//  6. Purity prevents the pain and gains instead; prevented damage is not dealt, so no Tamanoa.
//  7. Ancient Tomb deals 2 on its {C}{C} tap (tap_self_damage_any_mode).
//  8. Both players to 0 in one event is a DRAW (CR 104.4a) -- not a win.
//  9. An UNARMED board is the legacy model: raw pain, no triggers.
// 10. Routing: the deck lands on PreventDamageProvider, and the signature survives any one cut.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "deck/DeckLoader.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>

namespace
{

void EnsureCardsPd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const PreventDamageProvider& Prov()
{
    static const PreventDamageProvider p;
    return p;
}

// An ARMED board (what StampDeckTraits stamps for this deck), PreventDamageProvider attached.
GameState Board(bool armed = true)
{
    EnsureCardsPd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.dmg_events_armed    = armed;
    s.own_death_live      = armed;
    s.m_provider          = &Prov();
    return s;
}

int g_num = 1;
void Put(GameState& s, const std::string& name, int controller = 0)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = d->card;
    p.card.m_number     = g_num++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

ManaCost Cost(int generic, int w = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.black = b; c.red = r; c.green = g;
    return c;
}

bool Pay(GameState& s, const ManaCost& c)
{ return TapForCostShared(s, c, /*for_creature=*/false, /*available=*/nullptr, /*honor_legacy_cco=*/true); }

int Me(const GameState& s)  { return s.players[0].life; }
int Opp(const GameState& s) { return s.players[1].life; }

}   // namespace

TEST_CASE("Prevent Damage: Tamanoa gains on a painland's coloured tap, not on its painless {C} tap")
{
    GameState s = Board();
    Put(s, "Tamanoa");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, /*r=*/1)));   // {R}: the coloured mode -- 1 damage, Tamanoa +1
    CHECK(Me(s) == 20);
    CHECK(s.players[0].life_gained_this_turn == 1);
    for (const Permanent& p : s.battlefield) { CHECK(p.mana_tap_mark == 0); }   // flushed

    GameState c = Board();
    Put(c, "Tamanoa");
    Put(c, "Battlefield Forge");
    Put(c, "Vito, Thorn of the Dusk Rose");
    // Vito makes the Tamanoa trigger visible on the opponent: 1 gained -> opponent loses 1.
    REQUIRE(Pay(c, Cost(0, 0, 0, 1)));
    CHECK(Opp(c) == 19);

    // A GENERIC pip: with a provider that does not want our damage (Generic) the painland takes its
    // separate painless {C} ability -- no damage, no Tamanoa trigger...
    GameState g = Board();
    g.m_provider = &DefaultProvider();
    Put(g, "Tamanoa");
    Put(g, "Battlefield Forge");
    REQUIRE(Pay(g, Cost(1)));
    CHECK(Me(g) == 20);
    CHECK(g.players[0].life_gained_this_turn == 0);
    // ...while PreventDamageProvider (SelfDamageUseful with Tamanoa out) takes the damaging mode.
    GameState d = Board();
    Put(d, "Tamanoa");
    Put(d, "Battlefield Forge");
    REQUIRE(Pay(d, Cost(1)));
    CHECK(Me(d) == 20);
    CHECK(d.players[0].life_gained_this_turn == 1);
}

TEST_CASE("Prevent Damage: Faithmender doubles, two multiply, Bilbo's +1 applies first")
{
    {
        GameState s = Board();
        Put(s, "Rhox Faithmender");
        GainLife(s, 0, 3);
        CHECK(Me(s) == 26);
        CHECK(s.players[0].life_gained_this_turn == 6);
    }
    {
        GameState s = Board();
        Put(s, "Rhox Faithmender");
        Put(s, "Rhox Faithmender");
        GainLife(s, 0, 3);
        CHECK(Me(s) == 32);   // x4
    }
    {
        GameState s = Board();
        Put(s, "Rhox Faithmender");
        Put(s, "Bilbo, Birthday Celebrant");
        GainLife(s, 0, 3);
        CHECK(Me(s) == 28);   // (3+1)*2, never 3*2+1
    }
    {
        // Tamanoa + Faithmender on a painland tap: -1, then Tamanoa's 1 doubled to 2.
        GameState s = Board();
        Put(s, "Tamanoa");
        Put(s, "Rhox Faithmender");
        Put(s, "Battlefield Forge");
        REQUIRE(Pay(s, Cost(0, 0, 0, 1)));
        CHECK(Me(s) == 21);
    }
}

TEST_CASE("Prevent Damage: Vito drains the replaced amount; Dina drains 1 per event")
{
    GameState s = Board();
    Put(s, "Rhox Faithmender");
    Put(s, "Vito, Thorn of the Dusk Rose");
    Put(s, "Dina, Soul Steeper");
    GainLife(s, 0, 3);   // one event of 6
    CHECK(Opp(s) == 20 - 6 - 1);
    GainLife(s, 0, 1);   // a second event of 2
    CHECK(Opp(s) == 13 - 2 - 1);
    CHECK(s.opponent_lost_life_this_turn);
}

TEST_CASE("Prevent Damage: two Manabarbs land taps are two events -> two Dina drains")
{
    GameState s = Board();
    Put(s, "Manabarbs");
    Put(s, "Tamanoa");
    Put(s, "Dina, Soul Steeper");
    Put(s, "Forest");
    Put(s, "Forest");
    REQUIRE(Pay(s, Cost(0, 0, 0, 0, /*g=*/2)));
    CHECK(Me(s) == 20);        // -1 +1, twice
    CHECK(Opp(s) == 18);       // one drain per lifegain EVENT
    // Without Tamanoa the barbs are pure damage and nothing is drained.
    GameState t = Board();
    Put(t, "Manabarbs");
    Put(t, "Dina, Soul Steeper");
    Put(t, "Forest");
    Put(t, "Forest");
    REQUIRE(Pay(t, Cost(0, 0, 0, 0, 2)));
    CHECK(Me(t) == 18);
    CHECK(Opp(t) == 20);
}

TEST_CASE("Prevent Damage: lethal self-damage LOSES even with Tamanoa out, and stays lost")
{
    GameState s = Board();
    s.players[0].life = 1;
    Put(s, "Tamanoa");
    Put(s, "Rhox Faithmender");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, 1)));   // {R}: 1 damage takes us to 0 before Tamanoa resolves
    CHECK(Me(s) <= 0);
    CHECK(SelfHasLost(s));
    CHECK(s.players[0].life_gained_this_turn == 0);   // the trigger never resolved
    GainLife(s, 0, 50);                               // a later gain cannot revive us
    CHECK(SelfHasLost(s));
    s.players[1].life = 0;                            // ...and a later opponent death is no win
    CHECK_FALSE(OpponentHasLost(s));
}

TEST_CASE("Prevent Damage: Purity prevents the pain and gains; Tamanoa does NOT trigger on it")
{
    GameState s = Board();
    Put(s, "Purity");
    Put(s, "Tamanoa");
    Put(s, "Vito, Thorn of the Dusk Rose");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, 1)));
    CHECK(Me(s) == 21);    // no damage dealt; Purity's gain of 1
    CHECK(Opp(s) == 19);   // ONE lifegain event (Purity's) -- a Tamanoa trigger would make it 18

    // And a Manabarbs damage event is prevented the same way.
    GameState t = Board();
    Put(t, "Purity");
    Put(t, "Manabarbs");
    Put(t, "Forest");
    REQUIRE(Pay(t, Cost(0, 0, 0, 0, 1)));
    CHECK(Me(t) == 21);
}

TEST_CASE("Prevent Damage: Ancient Tomb deals 2 on its {C}{C} tap")
{
    GameState s = Board();
    Put(s, "Ancient Tomb");
    REQUIRE(Pay(s, Cost(2)));
    CHECK(Me(s) == 18);
    GameState t = Board();
    Put(t, "Tamanoa");
    Put(t, "Ancient Tomb");
    REQUIRE(Pay(t, Cost(2)));
    CHECK(Me(t) == 20);    // one 2-damage event, one Tamanoa trigger for 2
    // Its signature separates it from a painless {C}{C} land.
    CHECK(CardDatabase::Instance().Lookup("Ancient Tomb")->params.tap_self_damage_any_mode);
}

TEST_CASE("Prevent Damage: both players to 0 in one event is a DRAW, not a win")
{
    GameState s = Board();
    s.players[0].life = 1;
    s.players[1].life = 1;
    Put(s, "Tamanoa");
    dmgev::DealDamageEvent(s, 0, /*src_is_creature=*/false, /*combat=*/false,
                           /*to_self=*/1, /*to_opp=*/1, 0, "test sweeper");
    CHECK(SelfHasLost(s));
    CHECK_FALSE(OpponentHasLost(s));
    // Contrast: the opponent dying FIRST is a win, and later damage to us is a no-op (won-lock).
    GameState w = Board();
    w.players[1].life = 1;
    w.players[0].life = 1;
    dmgev::DealDamageEvent(w, 0, false, false, 0, 1, 0, "test burn");
    CHECK(OpponentHasLost(w));
    dmgev::DealDamageEvent(w, 0, false, false, 5, 0, 0, "test self-hit");
    CHECK(Me(w) == 1);
    CHECK(OpponentHasLost(w));
}

TEST_CASE("Prevent Damage: an UNARMED board keeps the legacy model byte-for-byte")
{
    GameState s = Board(/*armed=*/false);
    Put(s, "Tamanoa");
    Put(s, "Rhox Faithmender");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, 1)));
    CHECK(Me(s) == 19);     // raw pain, no trigger
    GainLife(s, 0, 3);
    CHECK(Me(s) == 22);     // no replacement
    for (const Permanent& p : s.battlefield) { CHECK(p.mana_tap_mark == 0); }
}

TEST_CASE("Prevent Damage routing: PreventDamageProvider, and the signature survives any one cut")
{
    EnsureCardsPd();
    const Decklist full = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Prevent Damage/Prevent Damage.cod"));
    CHECK(std::string(DetectDecisionProvider(full).Name()) == "PreventDamage");
    for (const char* cut : {"Tamanoa", "Manabarbs", "Vito, Thorn of the Dusk Rose", "Dina, Soul Steeper"})
    {
        Decklist d = full;
        d.mainboard.erase(std::remove_if(d.mainboard.begin(), d.mainboard.end(),
                                         [&](const Card& c) { return c.m_name.str() == cut; }),
                          d.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(d).Name()) == "PreventDamage", "cutting ", cut);
    }
}
