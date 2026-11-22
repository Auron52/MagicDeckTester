// Blood Sun step 3 (docs/design/blood-sun-pipeline.md 2, 3.4, 9.4, 10.3, 12): "All lands lose all abilities except mana
// abilities" -- the predicate (LandNonManaAbilitiesSuppressed), the mana-only projection every land chokepoint reads
// (EffectiveLandDef), City of Brass's triggered ping (tap_self_damage_is_trigger), the section 2.4 classification of
// every land param and its self-guard, the opponent's land entry, and BloodSunProvider's routing.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "cards/LandAbilityClasses.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandAbilityLoss.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsSun()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSun(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSun(const std::string& name, int number)
{
    Card c = DefSun(name).card;
    c.m_number = number;
    return c;
}

void PutSun(GameState& s, const std::string& name, int number, bool tapped = false, int controller = 0)
{
    Permanent p;
    p.card              = CardSun(name, number);
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
}

GameState BaseSun(int turn)
{
    static const VialProvider vp;
    EnsureCardsSun();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = true;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardSun("Swamp", 900 + k)); }
    return s;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountLands(const GameState& s, int controller = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == controller && p.card.IsLand()) { ++n; } }
    return n;
}

bool InGraveyard(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool PlayFromHandSun(GameState& s, const std::string& name, int number, int pin = -1)
{
    s.players[0].hand.push_back(CardSun(name, number));
    LandPlayOptions o;
    o.rad_mode = pin;
    return PlayLandFromHand(s, s.players[0].hand.size() - 1, DefSun(name), o);
}

void RemoveNum(GameState& s, int number)
{
    for (auto it = s.battlefield.begin(); it != s.battlefield.end(); ++it)
    { if (it->card.m_number == number) { s.battlefield.erase(it); return; } }
}

}   // namespace

// ---- card data and the classification ----------------------------------------------------------------------------

TEST_CASE("blood sun: params as the design states them; City of Brass's ping is a trigger")
{
    EnsureCardsSun();
    const CardDefinition& bs = DefSun("Blood Sun");
    CHECK(bs.params.lands_lose_nonmana_abilities);
    CHECK(bs.params.etb_self_draw == 1);
    CHECK(bs.card.IsEnchantment());
    CHECK(bs.card.m_mana_cost.ManaValue() == 3);
    const CardDefinition& city = DefSun("City of Brass");
    CHECK(city.params.tap_self_damage == 1);
    CHECK(city.params.tap_self_damage_is_trigger);
    // The projection drops the triggered ping and nothing else.
    REQUIRE(city.land_ability_loss_twin != nullptr);
    CHECK(city.land_ability_loss_twin->params.tap_self_damage == 0);
    CHECK(city.land_ability_loss_twin->params.produces.size() == 5);
    // A painland's damage is PART of its mana ability: no projection at all.
    CHECK(DefSun("Sunbaked Canyon").land_ability_loss_twin == nullptr);
    CHECK(DefSun("Swamp").land_ability_loss_twin == nullptr);
}

TEST_CASE("blood sun inventory: every land param in cards.json is classified exactly once")
{
    EnsureCardsSun();
    std::ifstream f(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    REQUIRE(f.good());
    const nlohmann::json root = nlohmann::json::parse(f);
    std::set<std::string> unclassified;
    int lands = 0;
    for (const nlohmann::json& e : root["cards"])
    {
        bool land = false;
        for (const auto& t : e.value("types", nlohmann::json::array())) { if (t == "Land") { land = true; } }
        if (!land || !e.contains("parameters")) { continue; }
        ++lands;
        for (auto it = e["parameters"].begin(); it != e["parameters"].end(); ++it)
        {
            if (landloss::ClassifyLandKey(it.key()) == landloss::LandKeyClass::Unclassified)
            { unclassified.insert(it.key() + " (" + e.value("name", std::string{}) + ")"); }
        }
    }
    CHECK(lands > 150);
    for (const std::string& k : unclassified)
    { FAIL_CHECK("land param not classified in cards/LandAbilityClasses.h (mana / gated / self-guard): ", k); }
    // ...and no key sits in two lists.
    std::set<std::string_view> seen;
    for (std::string_view k : landloss::kManaKeys)      { CHECK_MESSAGE(seen.insert(k).second, k); }
    for (std::string_view k : landloss::kGatedKeys)     { CHECK_MESSAGE(seen.insert(k).second, k); }
    for (std::string_view k : landloss::kSelfGuardKeys) { CHECK_MESSAGE(seen.insert(k).second, k); }
}

TEST_CASE("blood sun self-guard: the shipped list passes; a synthetic fetchland in it aborts")
{
    EnsureCardsSun();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Blood Sun/Blood Sun.cod"));
    CHECK(GoldFishRunner::LandAbilityLossSelfGuard(deck).empty());
    Decklist bad = deck;
    bad.mainboard.push_back(CardSun("Arid Mesa", 0));
    const std::string why = GoldFishRunner::LandAbilityLossSelfGuard(bad);
    CHECK(why.find("[bloodsun] Arid Mesa has ") == 0);
    CHECK(why.find("ungated land ability under Blood Sun") != std::string::npos);
    // ...and in the sideboard too (a wish could fetch it).
    Decklist side = deck;
    side.sideboard.push_back(CardSun("Mutavault", 0));
    CHECK_FALSE(GoldFishRunner::LandAbilityLossSelfGuard(side).empty());
}

TEST_CASE("blood sun routing: BloodSunProvider, NotAssessed")
{
    EnsureCardsSun();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Blood Sun/Blood Sun.cod"));
    const DecisionProvider& p = DetectDecisionProvider(deck);
    CHECK(std::string(p.Name()) == "BloodSun");
    const auto* dp = dynamic_cast<const DeckProvider*>(&p);
    REQUIRE(dp != nullptr);
    CHECK(dp->Certificate().state == CertState::NotAssessed);
}

// ---- the predicate --------------------------------------------------------------------------------------------------

TEST_CASE("blood sun predicate: either controller's Blood Sun; the deck stamp gates the walk")
{
    GameState s = BaseSun(3);
    PutSun(s, "Swamp", 1);
    CHECK_FALSE(LandNonManaAbilitiesSuppressed(s));
    PutSun(s, "Blood Sun", 2, false, /*controller=*/1);
    CHECK(LandNonManaAbilitiesSuppressed(s));
    s.battlefield.back().controller_index = 0;
    CHECK(LandNonManaAbilitiesSuppressed(s));
    s.deck_has_land_ability_loss = false;
    CHECK_FALSE(LandNonManaAbilitiesSuppressed(s));
}

TEST_CASE("blood sun G9: Lotus Vale and Lotus Field share an identity only under Blood Sun")
{
    GameState s = BaseSun(3);
    const CardDefinition& vale = DefSun("Lotus Vale");
    const CardDefinition& field = DefSun("Lotus Field");
    CHECK(EffectiveLandIdentity(s, vale) != EffectiveLandIdentity(s, field));
    PutSun(s, "Blood Sun", 1);
    CHECK(EffectiveLandIdentity(s, vale) == EffectiveLandIdentity(s, field));
    CHECK(EffectiveLandIdentity(s, vale) != EffectiveLandIdentity(s, DefSun("Scorched Ruins")));
}

// ---- section 3.4: the present lands under Blood Sun ---------------------------------------------------------------

TEST_CASE("blood sun 3.4: Rakdos Carnarium / Golgari Rot Farm enter untapped and bounce nothing")
{
    for (const char* karoo : { "Rakdos Carnarium", "Golgari Rot Farm" })
    {
        GameState s = BaseSun(3);
        PutSun(s, "Blood Sun", 1);
        PutSun(s, "Swamp", 2);
        REQUIRE(PlayFromHandSun(s, karoo, 50));
        const Permanent* k = FindNum(s, 50);
        REQUIRE_MESSAGE(k != nullptr, karoo);
        CHECK_FALSE(k->tapped);
        CHECK(FindNum(s, 2) != nullptr);          // the Swamp was not returned
        CHECK(CountLands(s) == 2);
        CHECK(s.players[0].hand.empty());
        // ...and alone: no self-bounce either.
        GameState a = BaseSun(1);
        PutSun(a, "Blood Sun", 1);
        REQUIRE(PlayFromHandSun(a, karoo, 51));
        REQUIRE(FindNum(a, 51) != nullptr);
        CHECK_FALSE(FindNum(a, 51)->tapped);
    }
    // Control: without Blood Sun the Karoo enters tapped and returns the Swamp.
    GameState c = BaseSun(3);
    PutSun(c, "Swamp", 2);
    REQUIRE(PlayFromHandSun(c, "Golgari Rot Farm", 50));
    CHECK(FindNum(c, 50)->tapped);
    CHECK(FindNum(c, 2) == nullptr);
}

TEST_CASE("blood sun 3.4: Blooming Marsh / Copperline Gorge are untapped with three other lands")
{
    for (const char* fast : { "Blooming Marsh", "Copperline Gorge" })
    {
        GameState s = BaseSun(5);
        for (int k = 0; k < 3; ++k) { PutSun(s, "Forest", 10 + k); }
        CHECK(LandWouldEnterTapped(s, DefSun(fast)));
        PutSun(s, "Blood Sun", 1);
        CHECK_FALSE(LandWouldEnterTapped(s, DefSun(fast)));
        REQUIRE(PlayFromHandSun(s, fast, 50));
        CHECK_FALSE(FindNum(s, 50)->tapped);
    }
}

TEST_CASE("blood sun 3.4: City of Brass pings without Blood Sun and not under it")
{
    ManaCost r; r.red = 1;
    GameState s = BaseSun(3);
    PutSun(s, "City of Brass", 10);
    REQUIRE(TapForCostDirect(s, r, /*for_creature=*/false));
    CHECK(s.players[0].life == 19);
    GameState b = BaseSun(3);
    PutSun(b, "City of Brass", 10);
    PutSun(b, "Blood Sun", 1);
    REQUIRE(TapForCostDirect(b, r, /*for_creature=*/false));
    CHECK(b.players[0].life == 20);
    // A painland's damage is part of its mana ability: it still hurts under Blood Sun.
    GameState p = BaseSun(3);
    PutSun(p, "Sunbaked Canyon", 10);
    PutSun(p, "Blood Sun", 1);
    REQUIRE(TapForCostDirect(p, r, /*for_creature=*/false));
    CHECK(p.players[0].life == 19);
}

TEST_CASE("blood sun 3.4: Lotus Vale / Scorched Ruins enter untapped and sacrifice nothing")
{
    for (const char* lotus : { "Lotus Vale", "Scorched Ruins" })
    {
        GameState s = BaseSun(3);
        PutSun(s, "Blood Sun", 1);
        PutSun(s, "Swamp", 2);
        PutSun(s, "Forest", 3);
        CHECK_FALSE(LakeDropWithoutVictim(s, DefSun(lotus)));
        CHECK(LotusSacrificePicks(s, 0, DefSun(lotus), -1).empty());
        CHECK(ResolveProvider(s).LandEntryPinCandidates(s, 0, DefSun(lotus)).empty());
        REQUIRE(PlayFromHandSun(s, lotus, 50));
        REQUIRE(FindNum(s, 50) != nullptr);
        CHECK_FALSE(FindNum(s, 50)->tapped);
        CHECK(CountLands(s) == 3);
        CHECK_FALSE(InGraveyard(s, lotus));
        // With NO other land (the replacement would send it to the graveyard): it enters.
        GameState a = BaseSun(1);
        PutSun(a, "Blood Sun", 1);
        CHECK_FALSE(LakeDropWithoutVictim(a, DefSun(lotus)));
        REQUIRE(PlayFromHandSun(a, lotus, 51));
        CHECK(FindNum(a, 51) != nullptr);
    }
}

TEST_CASE("blood sun 3.4: Lotus Field enters untapped and sacrifices nothing")
{
    GameState s = BaseSun(3);
    PutSun(s, "Blood Sun", 1);
    PutSun(s, "Swamp", 2);
    CHECK_FALSE(LakeDropWithoutVictim(s, DefSun("Lotus Field")));   // F-L2 stands down: nothing is sacrificed
    REQUIRE(PlayFromHandSun(s, "Lotus Field", 50));
    REQUIRE(FindNum(s, 50) != nullptr);
    CHECK_FALSE(FindNum(s, 50)->tapped);
    CHECK(FindNum(s, 2) != nullptr);
    CHECK(s.floating_mana.Total() == 0);                           // no tap-first float either
    // A PUT (Burgeoning's route, EnterLand) under Blood Sun: the same.
    GameState p = BaseSun(3);
    PutSun(p, "Blood Sun", 1);
    PutSun(p, "Swamp", 2);
    EnterLand(p, DefSun("Lotus Field"), 60, /*resolve_full_etb=*/true);
    REQUIRE(FindNum(p, 60) != nullptr);
    CHECK_FALSE(FindNum(p, 60)->tapped);
    CHECK(FindNum(p, 2) != nullptr);
    EnterLand(p, DefSun("Golgari Rot Farm"), 61, /*resolve_full_etb=*/true);
    CHECK_FALSE(FindNum(p, 61)->tapped);
    CHECK(CountLands(p) == 3);
}

// ---- timing: nothing is retroactive; Blood Sun leaving restores the abilities ------------------------------------

TEST_CASE("blood sun mid-turn: a Karoo that entered tapped stays tapped; a City tapped after it does not ping")
{
    GameState s = BaseSun(3);
    PutSun(s, "Swamp", 2);
    PutSun(s, "City of Brass", 3);
    REQUIRE(PlayFromHandSun(s, "Rakdos Carnarium", 50));            // before Blood Sun: tapped, bounces
    REQUIRE(FindNum(s, 50) != nullptr);
    CHECK(FindNum(s, 50)->tapped);
    const int lands_before = CountLands(s);
    PutSun(s, "Blood Sun", 1);                                       // Blood Sun resolves mid-turn
    CHECK(FindNum(s, 50)->tapped);                                   // not retroactive
    CHECK(CountLands(s) == lands_before);                            // the resolved bounce stays resolved
    ManaCost r; r.red = 1;
    for (Permanent& p : s.battlefield) { if (p.card.m_name.str() != "City of Brass") { p.tapped = true; } }
    REQUIRE(TapForCostDirect(s, r, /*for_creature=*/false));
    CHECK(s.players[0].life == 20);
}

TEST_CASE("blood sun leaving (synthetic exile): a land entering afterwards has its abilities again")
{
    GameState s = BaseSun(3);
    PutSun(s, "Blood Sun", 1);
    PutSun(s, "Swamp", 2);
    REQUIRE(PlayFromHandSun(s, "Golgari Rot Farm", 50));
    CHECK_FALSE(FindNum(s, 50)->tapped);
    RemoveNum(s, 1);                                                 // Blood Sun exiled
    CHECK_FALSE(LandNonManaAbilitiesSuppressed(s));
    CHECK_FALSE(FindNum(s, 50)->tapped);                             // the earlier entry is not redone
    s.players[0].lands_played_this_turn = 0;
    REQUIRE(PlayFromHandSun(s, "Rakdos Carnarium", 51));
    REQUIRE(FindNum(s, 51) != nullptr);
    CHECK(FindNum(s, 51)->tapped);                                   // enters tapped and bounces again
    CHECK(CountLands(s) == 2);
    ManaCost r; r.red = 1;
    GameState c = BaseSun(3);
    PutSun(c, "City of Brass", 3);
    PutSun(c, "Blood Sun", 1);
    RemoveNum(c, 1);
    REQUIRE(TapForCostDirect(c, r, /*for_creature=*/false));
    CHECK(c.players[0].life == 19);
}

// ---- the opponent's lands (G4) -------------------------------------------------------------------------------------

TEST_CASE("blood sun G4: the opponent's Karoo enters untapped under Blood Sun, tapped without")
{
    for (const bool sun : { false, true })
    {
        GameState s = BaseSun(3);
        s.opponent_plays_lands = true;
        if (sun) { PutSun(s, "Blood Sun", 1); }
        // The opponent's entry rules (origin, USER 2026-11-21): a Karoo bounces one of the head's lands, ITSELF when
        // it has no other -- so give the head a Swamp to return. Under Blood Sun the bounce is lost too.
        PutSun(s, "Swamp", 71, false, /*controller=*/1);
        s.players[1].hand.push_back(CardSun("Rakdos Carnarium", 70));
        const opponentdeck::LandPlayResult r = opponentdeck::PlayLeftmostLand(s);
        REQUIRE(r.played);
        const Permanent* k = FindNum(s, 70);
        REQUIRE(k != nullptr);
        CHECK(k->controller_index == 1);
        CHECK(k->tapped == !sun);
        CHECK((FindNum(s, 71) != nullptr) == sun);   // the Swamp stays only when the bounce is lost
    }
}
