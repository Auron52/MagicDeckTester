// Unit tests for Mossborn Hydra (2026-10-22, a Landfall screening candidate) and the two generic params
// it introduced -- CardParams::enters_with_plus_counters (an AS-ENTERS replacement, CR 614.1c) and
// CardParams::landfall_double_self_counters (the keyword action DOUBLE, CR 701.10e, from the landfall
// chokepoint OnLandEntered).
//
//  1. ENTERS WITH ONE COUNTER on every enter route: the universal cascade (FireEtbWatchers), the
//     executor's cast (EffectHandler::Resolve) and the rollout's cast (TurnSolver::ApplyPlan).
//  2. ONE LANDFALL DOUBLES (1 -> 2); an Evolving Wilds drop and crack is two landfalls (1 -> 2 -> 4).
//  3. COUNTER DOUBLERS: Primal Vigor doubles the enters-with counter (2) AND the doubling put (2 -> 6).
//  4. SIMULTANEITY (Genesis Wave): beside N lands it sees all N (1 -> 2^N); a co-entrant's ETB landfall
//     (Primeval Titan's lands, resolving before the Hydra's own cascade) already finds the counter; a
//     Doubling Season entering in the SAME event does not double the enters-with counter (CR 614.12) but
//     does double the later landfall put.
//  5. SEARCH: "cast the Hydra, THEN play the land" is enumerated and grows it to 2.
//  6. It is an ELEMENTAL (Omnath's dies watcher), and the doubling is overflow-guarded.
#include <doctest/doctest.h>

#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <cstddef>
#include <string>
#include <vector>

namespace
{

constexpr const char* kHydra = "Mossborn Hydra";

void LoadMh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefMh(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshMh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// A permanent pushed straight onto the battlefield WITHOUT the enter cascade (an old permanent).
int PutMh(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefMh(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// The Hydra ENTERING through the universal enter cascade (every enter route reaches it).
int EnterHydra(GameState& s, int number)
{
    const int idx = PutMh(s, kHydra, number, /*sick=*/true);
    FireEtbWatchers(s, 0, idx);
    return idx;
}

Card CardMh(const std::string& name, int number)
{
    Card c = DefMh(name).card;
    c.m_number = number;
    return c;
}

const Permanent* ByNumberMh(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int HydraCounters(const GameState& s, int number)
{
    const Permanent* p = ByNumberMh(s, number);
    REQUIRE(p != nullptr);
    return PlusCountersOn(*p);
}

int HydraNumberOnBf(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == kHydra) { return p.card.m_number; } }
    return -1;
}

}   // namespace

TEST_CASE("Mossborn Hydra: printed data and params as Scryfall says")
{
    LoadMh();
    const CardDefinition& h = DefMh(kHydra);
    CHECK(h.card.m_mana_cost.generic == 2);
    CHECK(h.card.m_mana_cost.green == 1);
    CHECK(h.card.m_mana_cost.ManaValue() == 3);
    CHECK(h.card.IsCreature());
    CHECK(h.card.m_power == 0);
    CHECK(h.card.m_toughness == 0);
    CHECK(h.card.HasKeyword(Keyword::Trample));
    CHECK(CardHasSubtype(h.card, "Elemental"));
    CHECK(CardHasSubtype(h.card, "Hydra"));
    CHECK(h.params.enters_with_plus_counters == 1);
    CHECK(h.params.landfall_double_self_counters);
    CHECK(h.landfall_watcher);                        // DefHasLandfallTrigger has the new term
    CHECK(DefHasLandfallTrigger(h.params));
}

TEST_CASE("Mossborn Hydra enters with one +1/+1 counter: a 1/1, never a 0/0")
{
    LoadMh();
    GameState s = FreshMh();
    const int idx = EnterHydra(s, 1);
    const Permanent& p = s.battlefield[static_cast<std::size_t>(idx)];
    CHECK(PlusCountersOn(p) == 1);
    CHECK(p.EffectivePower() == 1);
    CHECK(p.EffectiveToughness() == 1);
}

TEST_CASE("Mossborn Hydra: the executor's cast and the rollout's cast both enter it with the counter")
{
    LoadMh();
    // Executor: the resolved spell enters through EffectHandler.
    {
        GameState s = FreshMh();
        StackEntry e;
        e.type             = StackEntry::EntryType::Spell;
        e.source           = CardMh(kHydra, 1);
        e.controller_index = 0;
        EffectHandler::Resolve(s, e, DefMh(kHydra));
        REQUIRE(ByNumberMh(s, 1) != nullptr);
        CHECK(HydraCounters(s, 1) == 1);
    }
    // Rollout: a plan casting it, applied by TurnSolver.
    {
        GameState s = FreshMh();
        for (int i = 0; i < 3; ++i) { PutMh(s, "Forest", 10 + i); }
        s.players[0].hand.push_back(CardMh(kHydra, 1));
        bool cast_seen = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            bool casts = false;
            for (const Action& a : p.actions) { if (a.card_name.str() == kHydra) { casts = true; } }
            if (!casts) { continue; }
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, true);
            CHECK(HydraCounters(t, 1) == 1);
            cast_seen = true;
            break;
        }
        CHECK(cast_seen);
    }
}

TEST_CASE("Mossborn Hydra: a generic put (a graveyard return) enters it with the counter too -- once")
{
    LoadMh();
    GameState s = FreshMh();
    const int idx = PutCardOntoBattlefield(s, 0, CardMh(kHydra, 1), "test");
    REQUIRE(idx >= 0);
    CHECK(HydraCounters(s, 1) == 1);
}

TEST_CASE("Mossborn Hydra: one landfall doubles 1 -> 2, a second 2 -> 4 (played and put lands alike)")
{
    LoadMh();
    GameState s = FreshMh();
    EnterHydra(s, 1);
    s.players[0].hand.push_back(CardMh("Forest", 2));
    REQUIRE(PlayLandFromHand(s, 0, DefMh("Forest"), LandPlayOptions{}));   // a PLAYED land
    CHECK(HydraCounters(s, 1) == 2);
    EnterLand(s, DefMh("Forest"), 3);                                       // a PUT land
    CHECK(HydraCounters(s, 1) == 4);
    CHECK(ByNumberMh(s, 1)->EffectivePower() == 4);
}

TEST_CASE("Mossborn Hydra: an Evolving Wilds drop and crack is two landfalls -- 1 -> 2 -> 4")
{
    LoadMh();
    GameState s = FreshMh();
    EnterHydra(s, 1);
    s.players[0].hand.push_back(CardMh("Evolving Wilds", 10));
    s.players[0].library.push_back(CardMh("Forest", 50));
    REQUIRE(PlayLandFromHand(s, 0, DefMh("Evolving Wilds"), LandPlayOptions{}));   // Wilds enters
    CHECK(HydraCounters(s, 1) == 2);
    // The crack: {T} (paid by the caller), sacrifice, search -> the basic enters tapped (EnterLand).
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 10) { p.tapped = true; } }
    ApplyPermAbility(s, 0, 10, PermAbilityMode::SacLandSearch);
    CHECK(HydraCounters(s, 1) == 4);
}

TEST_CASE("Mossborn Hydra with Primal Vigor: enters with 2, and the first landfall puts 2x2 more -> 6")
{
    LoadMh();
    GameState s = FreshMh();
    PutMh(s, "Primal Vigor", 5);
    EnterHydra(s, 1);
    CHECK(HydraCounters(s, 1) == 2);                  // the enters-with counter is a put: doubled
    EnterLand(s, DefMh("Forest"), 3);
    CHECK(HydraCounters(s, 1) == 6);                  // "double" puts 2, Primal Vigor makes it 4
}

TEST_CASE("Genesis Wave: a Hydra put beside three lands sees all three -- 1 -> 8")
{
    LoadMh();
    GameState s = FreshMh();
    s.players[0].library.push_back(CardMh(kHydra, 40));
    for (int i = 0; i < 3; ++i) { s.players[0].library.push_back(CardMh("Forest", 41 + i)); }
    PerformGenesisWave(s, 0, 4, "Genesis Wave");
    REQUIRE(HydraNumberOnBf(s) == 40);
    CHECK(HydraCounters(s, 40) == 8);
    // Lands FIRST in reveal order: the same -- landfall waits for every entrant (pass 3).
    GameState t = FreshMh();
    for (int i = 0; i < 3; ++i) { t.players[0].library.push_back(CardMh("Forest", 41 + i)); }
    t.players[0].library.push_back(CardMh(kHydra, 40));
    PerformGenesisWave(t, 0, 4, "Genesis Wave");
    CHECK(HydraCounters(t, 40) == 8);
}

TEST_CASE("Genesis Wave: a co-entering Primeval Titan's lands find the Hydra already holding its counter")
{
    LoadMh();
    // Reveal order Titan, Hydra, Forest (+ three dead cards); two Forests stay below for Titan's ETB.
    // Titan's ETB resolves in pass 2a BEFORE the Hydra's own cascade, and its two lands trigger the Hydra
    // then: with the as-enters counter applied in pass 1 that is 1 -> 2 -> 4, and the Wave's Forest
    // (pass 3) makes 8. Applied only at the Hydra's own cascade, Titan's landfalls would double ZERO.
    GameState s = FreshMh();
    s.players[0].library.push_back(CardMh("Primeval Titan", 40));
    s.players[0].library.push_back(CardMh(kHydra, 41));
    s.players[0].library.push_back(CardMh("Forest", 42));
    for (int i = 0; i < 3; ++i) { s.players[0].library.push_back(CardMh("Swords to Plowshares", 43 + i)); }
    s.players[0].library.push_back(CardMh("Forest", 50));
    s.players[0].library.push_back(CardMh("Forest", 51));
    PerformGenesisWave(s, 0, 6, "Genesis Wave");
    REQUIRE(HydraNumberOnBf(s) == 41);
    int forests = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Forest") { ++forests; } }
    REQUIRE(forests == 3);                            // the Wave's Forest + Titan's two
    CHECK(HydraCounters(s, 41) == 8);
}

TEST_CASE("Genesis Wave: a Doubling Season entering in the SAME event does not double the enters-with counter")
{
    LoadMh();
    // CR 614.12: only replacement effects that already exist apply to how a permanent enters. The
    // landfall trigger resolves afterwards, with the Season on the battlefield: it puts 1, doubled -> 3.
    GameState s = FreshMh();
    s.players[0].library.push_back(CardMh("Doubling Season", 40));
    s.players[0].library.push_back(CardMh(kHydra, 41));
    PerformGenesisWave(s, 0, 5, "Genesis Wave");
    CHECK(HydraCounters(s, 41) == 1);
    GameState t = FreshMh();
    t.players[0].library.push_back(CardMh("Doubling Season", 40));
    t.players[0].library.push_back(CardMh(kHydra, 41));
    t.players[0].library.push_back(CardMh("Forest", 42));
    PerformGenesisWave(t, 0, 5, "Genesis Wave");
    CHECK(HydraCounters(t, 41) == 3);
    // ...whereas a Season ALREADY on the battlefield doubles it.
    GameState u = FreshMh();
    PutMh(u, "Doubling Season", 5);
    u.players[0].library.push_back(CardMh(kHydra, 41));
    PerformGenesisWave(u, 0, 5, "Genesis Wave");
    CHECK(HydraCounters(u, 41) == 2);
}

TEST_CASE("Land-drop timing: 'cast the Hydra, THEN play the land' is enumerated and grows it to 2")
{
    LoadMh();
    GameState s = FreshMh();
    for (int i = 0; i < 3; ++i) { PutMh(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(CardMh(kHydra, 1));
    s.players[0].hand.push_back(CardMh("Forest", 2));
    const TurnSolver::Plan* after = nullptr;
    const TurnSolver::Plan* first = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool casts = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == kHydra) { casts = true; } }
        if (!casts || p.land_to_play != "Forest") { continue; }
        if (p.land_slot == 0) { if (first == nullptr) { first = &p; } }
        else if (after == nullptr) { after = &p; }
    }
    REQUIRE(first != nullptr);                        // both orders stay enumerable (lossless)
    REQUIRE(after != nullptr);
    GameState a = s;
    TurnSolver::ApplyPlan(a, *after, true);
    CHECK(HydraCounters(a, 1) == 2);                  // the land entered AFTER the Hydra
    GameState b = s;
    TurnSolver::ApplyPlan(b, *first, true);
    CHECK(HydraCounters(b, 1) == 1);
}

TEST_CASE("Mossborn Hydra is an Elemental: Omnath, Locus of Rage deals 3 when it dies")
{
    LoadMh();
    GameState s = FreshMh();
    PutMh(s, "Omnath, Locus of Rage", 2);
    EnterHydra(s, 1);
    const Card dead = ByNumberMh(s, 1)->card;
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    {
        if (s.battlefield[i].card.m_number == 1)
        { s.battlefield.erase(s.battlefield.begin() + static_cast<std::ptrdiff_t>(i)); break; }
    }
    s.players[0].graveyard.push_back(dead);
    OnCreatureDies(s, 0, dead, /*dead_was_token=*/false, /*dead_minus_counters=*/0);
    CHECK(s.players[1].life == 17);
}

TEST_CASE("DoublePlusCounters: doubling zero puts nothing; the count is clamped, never overflows")
{
    LoadMh();
    GameState s = FreshMh();
    const int idx = PutMh(s, kHydra, 1);              // no enter cascade: zero counters
    CHECK(DoublePlusCounters(s, s.battlefield[static_cast<std::size_t>(idx)]) == 0);
    CHECK(PlusCountersOn(s.battlefield[static_cast<std::size_t>(idx)]) == 0);
    AddPlusCounters(s.battlefield[static_cast<std::size_t>(idx)], kPlusCounterCap - 5);
    EnterLand(s, DefMh("Forest"), 3);
    CHECK(HydraCounters(s, 1) == kPlusCounterCap);
    EnterLand(s, DefMh("Forest"), 4);
    CHECK(HydraCounters(s, 1) == kPlusCounterCap);
}
