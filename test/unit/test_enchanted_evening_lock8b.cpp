// Enchanted Evening lock cluster, STEP 8b (2026-11-26): two SOUND dominance folds in EnchantedEveningProvider.
//   (1) THE DEAD-LAND FOLD (PlanListAdmit, EeDeadLandFoldLive): under Enchanted Evening + Opalescence, with no mana left
//       this turn and at most seven cards in hand, a plan that plays a land (it dies at once) is dropped for its no-land
//       twin. A plan that exiles our own Enchanted Evening / Opalescence with the Wave (it ends the lock) keeps its land.
//   (2) UTOPIA SPRAWL's colour axis held to {G, W} (EtbChoiceCandidates), re-checked over our cards at runtime.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
const CardDefinition& L8bDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card L8bCard(const std::string& name, int number)
{
    Card c = L8bDef(name).card;
    c.m_number = number;
    return c;
}
void L8bPut(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = L8bCard(name, number);
    p.controller_index = p.owner_index = controller;
    if (L8bDef(name).params.fading_counters > 0) { p.fade_counters = L8bDef(name).params.fading_counters; }
    s.battlefield.push_back(p);
}
const EnchantedEveningProvider& L8bProv()
{
    static const EnchantedEveningProvider prov;
    return prov;
}
// Our turn 5, main 1, the land drop open: Enchanted Evening + Opalescence + Parallax Wave on our side (the lock: no
// land of ours survives), a Forest and a Windswept Heath in hand.
GameState L8bLockBoard()
{
    L8bDef("Forest");
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 0;
    s.deck_float_wild_exact = true;
    s.deck_has_type_statics = true;
    s.m_provider = &L8bProv();
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L8bCard("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L8bCard("Plains", 100900 + k)); }
    L8bPut(s, "Enchanted Evening", 51);
    L8bPut(s, "Opalescence", 52);
    L8bPut(s, "Parallax Wave", 53);
    s.players[0].hand.push_back(L8bCard("Forest", 60));
    s.players[0].hand.push_back(L8bCard("Windswept Heath", 61));
    RefreshTypeStatics(s);
    return s;
}
int LandPlans(const std::vector<TurnSolver::Plan>& plans)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans) { if (!p.land_to_play.empty()) { ++n; } }
    return n;
}
bool HasWavePlanOn(const std::vector<TurnSolver::Plan>& plans, int victim, bool with_land)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_to_play.empty() == with_land) { continue; }
        for (const Action& a : p.actions)
        {
            if (a.ability_mode == Action::AbilityMode::FadeExileCreature && a.sac_victim_id == victim) { return true; }
        }
    }
    return false;
}
} // namespace

TEST_CASE("EE lock step 8b: under the lock with no mana and <= 7 cards, a dead land play is folded into holding it")
{
    GameState s = L8bLockBoard();
    REQUIRE(LandDiesToTypeStatics(s, L8bDef("Forest")));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    REQUIRE_FALSE(plans.empty());
    // Every land plan that does not end the lock is gone; its twin (the same activations, no land) stays.
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_to_play.empty()) { continue; }
        bool ends_lock = false;
        for (const Action& a : p.actions)
        {
            if (a.ability_mode == Action::AbilityMode::FadeExileCreature && (a.sac_victim_id == 51 || a.sac_victim_id == 52))
            { ends_lock = true; }
        }
        CHECK_MESSAGE(ends_lock, "a dead land plan survived: ", p.land_to_play);
    }
    // The Wave activations are still offered without a land (the twins), self-reset included.
    CHECK(HasWavePlanOn(plans, 53, /*with_land=*/false));
    // Exiling our own Enchanted Evening ends the lock: that plan keeps its land variant (the land then survives).
    CHECK(HasWavePlanOn(plans, 51, /*with_land=*/false));
    CHECK(HasWavePlanOn(plans, 51, /*with_land=*/true));
}

TEST_CASE("EE lock step 8b: the dead-land fold is OFF with eight cards in hand, with floating mana, or without the lock")
{
    SUBCASE("eight cards: the held land could cost a nonland cleanup discard")
    {
        GameState s = L8bLockBoard();
        for (int k = 0; k < 6; ++k) { s.players[0].hand.push_back(L8bCard("Calming Verse", 70 + k)); }
        REQUIRE(s.players[0].hand.size() == 8);
        CHECK(LandPlans(TurnSolver::EnumerateMainPlans(s, true)) > 0);
    }
    SUBCASE("seven cards: folded")
    {
        GameState s = L8bLockBoard();
        for (int k = 0; k < 5; ++k) { s.players[0].hand.push_back(L8bCard("Calming Verse", 70 + k)); }
        REQUIRE(s.players[0].hand.size() == 7);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.land_to_play.empty()) { continue; }
            bool ends_lock = false;
            for (const Action& a : p.actions)
            { if (a.sac_victim_id == 51 || a.sac_victim_id == 52) { ends_lock = true; } }
            CHECK(ends_lock);
        }
    }
    SUBCASE("floating mana: a cast (and a Presence draw) is still possible this turn")
    {
        GameState s = L8bLockBoard();
        s.floating_mana.green = 1;
        CHECK(LandPlans(TurnSolver::EnumerateMainPlans(s, true)) > 0);
    }
    SUBCASE("no Opalescence: the land lives")
    {
        GameState s = L8bLockBoard();
        s.battlefield.erase(s.battlefield.begin() + 1);   // Opalescence
        RefreshTypeStatics(s);
        REQUIRE_FALSE(LandDiesToTypeStatics(s, L8bDef("Forest")));
        CHECK(LandPlans(TurnSolver::EnumerateMainPlans(s, true)) > 0);
    }
    SUBCASE("the land drop is spent: nothing to fold, nothing breaks")
    {
        GameState s = L8bLockBoard();
        s.players[0].lands_played_this_turn = 1;
        CHECK(LandPlans(TurnSolver::EnumerateMainPlans(s, true)) == 0);
    }
}

TEST_CASE("EE lock step 8b: the dead-land fold lives in the EE provider only")
{
    GameState s = L8bLockBoard();
    s.m_provider = nullptr;   // the base provider: no fold
    CHECK(LandPlans(TurnSolver::EnumerateMainPlans(s, true)) > 0);
}

TEST_CASE("EE lock step 8b: Utopia Sprawl's colour axis is held to G and W; the full fan returns when the proof breaks")
{
    L8bDef("Forest");
    GameState s;
    s.active_player_index = 0;
    s.m_provider = &L8bProv();
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L8bCard("Enchanted Evening", 900 + k)); }   // {W/U}
    s.players[0].hand.push_back(L8bCard("Utopia Sprawl", 60));
    const CardDefinition& sprawl = L8bDef("Utopia Sprawl");
    const std::vector<int> base = L8bProv().DeckProvider::EtbChoiceCandidates(s, 0, sprawl);
    CHECK(base.size() == 5);
    const std::vector<int> got = L8bProv().EtbChoiceCandidates(s, 0, sprawl);
    const std::set<int> want = { static_cast<int>(Color::Green) + 1, static_cast<int>(Color::White) + 1 };
    CHECK(std::set<int>(got.begin(), got.end()) == want);
    CHECK(got.size() == 2);
    // A plain blue pip anywhere in our cards (here Brainstorm in the library) breaks the proof: the full fan.
    GameState b = s;
    b.players[0].library.push_back(L8bCard("Brainstorm", 950));
    CHECK(L8bProv().EtbChoiceCandidates(b, 0, sprawl).size() == 5);
    // So does a red card in hand.
    GameState r = s;
    r.players[0].hand.push_back(L8bCard("Lightning Bolt", 951));
    CHECK(L8bProv().EtbChoiceCandidates(r, 0, sprawl).size() == 5);
    // Wild Growth has no colour choice: untouched (empty fan).
    CHECK(L8bProv().EtbChoiceCandidates(s, 0, L8bDef("Wild Growth")).empty());
}

TEST_CASE("EE lock step 8b: Parallax Wave own-target fold -- Exploration withdrawn while no Calming Verse is left")
{
    // Opalescence + Wave + Exploration + Presence on our side, no Enchanted Evening (our lands live; the land drop is
    // spent so only activations are in the menu).
    auto board = [] {
        GameState s = L8bLockBoard();
        s.battlefield.erase(s.battlefield.begin());   // Enchanted Evening
        s.players[0].hand.clear();
        s.players[0].lands_played_this_turn = 1;
        L8bPut(s, "Exploration", 54);
        L8bPut(s, "Enchantress's Presence", 55);
        RefreshTypeStatics(s);
        return s;
    };
    {
        GameState s = board();
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK_FALSE(HasWavePlanOn(plans, 54, false));   // Exploration: folded
        CHECK(HasWavePlanOn(plans, 55, false));         // Presence: kept (mandatory draw)
        CHECK(HasWavePlanOn(plans, 53, false));         // the self-reset: kept
    }
    {
        GameState s = board();
        s.players[0].library.push_back(L8bCard("Calming Verse", 960));   // F2 is live: kept
        CHECK(HasWavePlanOn(TurnSolver::EnumerateMainPlans(s, true), 54, false));
    }
    {
        GameState s = board();
        s.players[0].hand.push_back(L8bCard("Calming Verse", 961));
        CHECK(HasWavePlanOn(TurnSolver::EnumerateMainPlans(s, true), 54, false));
    }
    {
        GameState s = board();
        s.m_provider = nullptr;   // the base provider folds nothing
        CHECK(HasWavePlanOn(TurnSolver::EnumerateMainPlans(s, true), 54, false));
    }
}
