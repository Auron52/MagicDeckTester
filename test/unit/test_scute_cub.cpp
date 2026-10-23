// Unit tests for Scute Swarm and Scythecat Cub (2026-10-23, Landfall screening candidates) and the generic
// params they introduced -- CardParams::landfall_copy_self_at_lands (Scute's "six or more lands: a token
// copy instead"), landfall_target_any_creature + landfall_target_second_doubles (the Cub's "target creature
// you control; the second resolution this turn doubles instead") -- plus the two engine rules they needed:
// the TOKEN FLOOR of a simultaneous land event (LandfallSimultaneousScope) and DOUBLINGS-LAST ordering of
// one land's landfall triggers.
//
//  1. Printed data and params as Scryfall says; both are landfall watchers.
//  2. SCUTE: fewer than six lands -> a 1/1 green Insect; six or more -> a token COPY that carries the
//     ability (two swarms -> four on the next land).
//  3. SCUTE SIMULTANEITY: Primeval Titan's two lands are ONE event -- the copy made by the first land does
//     not trigger for the second (1 swarm -> 3, never 4); Genesis Wave the same.
//  4. CUB: first resolution +1, second DOUBLES, third +1 again; a cleared count starts over; a Cub
//     that has resolved is never identity-folded with one that has not.
//  5. CUB TARGET: never a token made by another trigger of the same land (a Baloths Beast); a doubling
//     goes to the attack-ready body with the MOST counters.
//  6. DOUBLINGS LAST: a Cub ahead of Phylath on the battlefield still doubles AFTER Phylath's +4; a Cub's
//     +1 on a Mossborn Hydra lands before the Hydra doubles.
#include <doctest/doctest.h>

#include "ai/LandPlay.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

constexpr const char* kScute = "Scute Swarm";
constexpr const char* kCub   = "Scythecat Cub";

void LoadSc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSc(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshSc()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// A permanent pushed straight onto the battlefield WITHOUT the enter cascade (an old permanent).
int PutSc(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefSc(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card CardSc(const std::string& name, int number)
{
    Card c = DefSc(name).card;
    c.m_number = number;
    return c;
}

const Permanent* ByNumberSc(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountNamedSc(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

int CountersOf(const GameState& s, int number)
{
    const Permanent* p = ByNumberSc(s, number);
    REQUIRE(p != nullptr);
    return PlusCountersOn(*p);
}

void Lands(GameState& s, int n, int first_number)
{
    for (int i = 0; i < n; ++i) { PutSc(s, "Forest", first_number + i); }
}

}   // namespace

TEST_CASE("Scute Swarm / Scythecat Cub: printed data and params as Scryfall says")
{
    LoadSc();
    const CardDefinition& sw = DefSc(kScute);
    CHECK(sw.card.m_mana_cost.generic == 2);
    CHECK(sw.card.m_mana_cost.green == 1);
    CHECK(sw.card.m_mana_cost.ManaValue() == 3);
    CHECK(sw.card.m_power == 1);
    CHECK(sw.card.m_toughness == 1);
    CHECK(CardHasSubtype(sw.card, "Insect"));
    CHECK(sw.params.landfall_creates_tokens == 1);
    CHECK(sw.params.landfall_copy_self_at_lands == 6);
    CHECK(sw.landfall_watcher);

    const CardDefinition& cb = DefSc(kCub);
    CHECK(cb.card.m_mana_cost.generic == 1);
    CHECK(cb.card.m_mana_cost.green == 1);
    CHECK(cb.card.m_mana_cost.ManaValue() == 2);
    CHECK(cb.card.m_power == 2);
    CHECK(cb.card.m_toughness == 2);
    CHECK(cb.card.HasKeyword(Keyword::Trample));
    CHECK(CardHasSubtype(cb.card, "Cat"));
    CHECK(cb.params.landfall_target_counters == 1);
    CHECK(cb.params.landfall_target_any_creature);
    CHECK(cb.params.landfall_target_second_doubles);
    CHECK(DefHasTargetedLandfall(cb.params));
    CHECK(cb.landfall_watcher);
}

TEST_CASE("Scute Swarm: under six lands a 1/1 green Insect; at six a token COPY that carries the ability")
{
    LoadSc();
    GameState s = FreshSc();
    PutSc(s, kScute, 1);
    Lands(s, 4, 10);
    EnterLand(s, DefSc("Forest"), 20);                      // 5 lands: an Insect
    CHECK(CountNamedSc(s, kScute) == 1);
    int insects = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token) { continue; }
        CHECK(p.card.m_name.str() != kScute);
        CHECK(CardHasSubtype(p.card, "Insect"));
        CHECK(p.card.m_power == 1);
        CHECK(p.entered_this_turn);
        ++insects;
    }
    CHECK(insects == 1);

    EnterLand(s, DefSc("Forest"), 21);                      // 6 lands: a copy
    CHECK(CountNamedSc(s, kScute) == 2);
    const Permanent* copy = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.is_token && p.card.m_name.str() == kScute) { copy = &p; } }
    REQUIRE(copy != nullptr);
    CHECK(copy->entered_this_turn);                         // summoning sick
    const CardDefinition* cd = CardDatabase::Instance().LookupCached(copy->card);
    REQUIRE(cd != nullptr);
    CHECK(cd->landfall_watcher);                            // the copy has the ability

    EnterLand(s, DefSc("Forest"), 22);                      // both trigger: 2 -> 4
    CHECK(CountNamedSc(s, kScute) == 4);
}

TEST_CASE("Scute Swarm: Primeval Titan's two lands are ONE event -- 1 swarm becomes 3, never 4")
{
    LoadSc();
    GameState s = FreshSc();
    PutSc(s, kScute, 1);
    Lands(s, 6, 10);
    s.players[0].library.push_back(CardSc("Forest", 30));
    s.players[0].library.push_back(CardSc("Forest", 31));
    PerformLandSearch(s, 0, LandSearchSpecOf(DefSc("Primeval Titan").params), "Primeval Titan", -1);
    REQUIRE(CountNamedSc(s, "Forest") == 8);
    CHECK(CountNamedSc(s, kScute) == 3);                    // the first copy did not see the second land
}

TEST_CASE("Scute Swarm: Genesis Wave's lands are one event -- each pre-existing swarm triggers per land")
{
    LoadSc();
    GameState s = FreshSc();
    PutSc(s, kScute, 1);
    Lands(s, 6, 10);
    for (int i = 0; i < 3; ++i) { s.players[0].library.push_back(CardSc("Forest", 40 + i)); }
    PerformGenesisWave(s, 0, 3, "Genesis Wave");
    REQUIRE(CountNamedSc(s, "Forest") == 9);
    CHECK(CountNamedSc(s, kScute) == 4);                    // 1 + 3 copies (sequential would be 8)
}

TEST_CASE("Scythecat Cub: +1, then DOUBLE on the second resolution, then +1 again; the untap step resets")
{
    LoadSc();
    GameState s = FreshSc();
    const int cub = 1;
    PutSc(s, kCub, cub);                                    // the only creature: its own target
    Lands(s, 3, 10);
    EnterLand(s, DefSc("Forest"), 20);
    CHECK(CountersOf(s, cub) == 1);                         // first: +1
    CHECK(ByNumberSc(s, cub)->landfall_resolved_once);
    CHECK_FALSE(ByNumberSc(s, cub)->landfall_resolved_twice);
    EnterLand(s, DefSc("Forest"), 21);
    CHECK(CountersOf(s, cub) == 2);                         // second: doubles 1 -> 2
    CHECK(ByNumberSc(s, cub)->landfall_resolved_twice);
    EnterLand(s, DefSc("Forest"), 22);
    CHECK(CountersOf(s, cub) == 3);                         // third: +1 again
    EnterLand(s, DefSc("Forest"), 23);
    CHECK(CountersOf(s, cub) == 4);                         // fourth: +1

    // Cleared (the untap step's reset, both worlds), the next resolution is a FIRST again, the one after
    // it doubles.
    for (Permanent& p : s.battlefield)
    { if (p.card.m_number == cub) { p.landfall_resolved_once = false; p.landfall_resolved_twice = false; } }
    EnterLand(s, DefSc("Forest"), 24);
    CHECK(CountersOf(s, cub) == 5);
    EnterLand(s, DefSc("Forest"), 25);
    CHECK(CountersOf(s, cub) == 10);
}

TEST_CASE("Scythecat Cub: a Cub that resolved this turn is never folded with one that did not")
{
    LoadSc();
    GameState s = FreshSc();
    PutSc(s, kCub, 1);
    PutSc(s, kCub, 2);
    std::uint64_t k1 = 0, k2 = 0;
    REQUIRE(LandfallTargetFoldKey(s, s.battlefield[0], k1));
    REQUIRE(LandfallTargetFoldKey(s, s.battlefield[1], k2));
    CHECK(k1 == k2);
    s.battlefield[0].landfall_resolved_once = true;
    CHECK_FALSE(LandfallTargetFoldKey(s, s.battlefield[0], k1));
    CHECK(RemovalSelfTargetFoldKey(s, s.battlefield[0]) != RemovalSelfTargetFoldKey(s, s.battlefield[1]));
}

TEST_CASE("Scythecat Cub: a token another trigger of the SAME land made is not a legal target")
{
    LoadSc();
    GameState s = FreshSc();
    // The Baloths resolves FIRST (battlefield order) and makes a Beast; the Cub's trigger was put on the
    // stack with the Beast not yet in existence, so it can only take the Cub or the Baloths.
    PutSc(s, "Rampaging Baloths", 1);
    const int cub = 2;
    PutSc(s, kCub, cub);
    Lands(s, 3, 10);
    EnterLand(s, DefSc("Forest"), 20);
    for (const Permanent& p : s.battlefield)
    {
        if (p.is_token) { CHECK(PlusCountersOn(p) == 0); }
    }
    // The base order: an attack-ready body first, then the biggest -- the 6/6 Baloths.
    CHECK(CountersOf(s, 1) == 1);
    CHECK(CountersOf(s, cub) == 0);
}

TEST_CASE("Scythecat Cub: the doubling goes to the attack-ready body with the MOST +1/+1 counters")
{
    LoadSc();
    GameState s = FreshSc();
    const int cub = 1;
    PutSc(s, kCub, cub);
    const int big = 2;   // a 6/6 with no counters: the put's front pick, worth nothing to a doubling
    PutSc(s, "Rampaging Baloths", big);
    const int grown = 3;
    PutSc(s, "Avenger of Zendikar", grown);
    s.battlefield.back().counters.Add(Counter{Counter::Type::PlusOnePlusOne, 3});
    const int sick = 4;  // the most counters of all, but it cannot attack this turn
    PutSc(s, "Avenger of Zendikar", sick, /*sick=*/true);
    s.battlefield.back().counters.Add(Counter{Counter::Type::PlusOnePlusOne, 5});
    s.battlefield[0].landfall_resolved_once = true;         // the Cub resolved once already this turn
    Lands(s, 3, 10);
    // No Plants anywhere, so the Avengers' own landfall puts nothing.
    EnterLand(s, DefSc("Forest"), 20);
    CHECK(CountersOf(s, grown) == 6);                       // doubled 3 -> 6
    CHECK(CountersOf(s, sick) == 5);
    CHECK(CountersOf(s, big) == 0);
}

TEST_CASE("Doublings last: a Cub ahead of Phylath doubles AFTER Phylath's four counters")
{
    LoadSc();
    GameState s = FreshSc();
    const int cub = 1;
    PutSc(s, kCub, cub);                                    // earlier on the battlefield than Phylath
    s.battlefield.back().landfall_resolved_once = true;     // this landfall is its SECOND resolution
    PutSc(s, "Phylath, World Sculptor", 2);
    const int plant = 3;
    PutSc(s, "Avenger of Zendikar", 4);                     // any body; the Plant is a token below
    CreateToken(s, 0, 0, 1, std::vector<std::string>{ "Plant" }, "G", {});
    Permanent& pl = s.battlefield.back();
    pl.entered_this_turn = false;
    pl.card.m_number     = plant;                           // a stable number for the checks
    pl.is_token          = false;                           // an OLD plant, not one this event made
    Lands(s, 3, 10);
    EnterLand(s, DefSc("Forest"), 20);
    // Avenger +1 on each Plant (1), Phylath +4 (5), THEN the Cub doubles (10). Battlefield order would
    // have doubled first: 2c + 5 = 5.
    CHECK(CountersOf(s, plant) == 10);
}

TEST_CASE("Doublings last: a Cub's +1 on a Mossborn Hydra lands BEFORE the Hydra doubles")
{
    LoadSc();
    GameState s = FreshSc();
    const int hydra = 1;
    PutSc(s, "Mossborn Hydra", hydra);                      // earlier on the battlefield than the Cub
    s.battlefield.back().counters.Add(Counter{Counter::Type::PlusOnePlusOne, 4});   // a 4/4
    const int cub = 2;
    PutSc(s, kCub, cub);
    Lands(s, 3, 10);
    EnterLand(s, DefSc("Forest"), 20);
    // The Cub's first resolution puts +1 on the biggest attack-ready body (the 4/4 Hydra) and THEN the
    // Hydra doubles: (4 + 1) * 2 = 10, not 4 * 2 + 1 = 9.
    CHECK(CountersOf(s, hydra) == 10);
    CHECK(CountersOf(s, cub) == 0);
}
