// Enchanted Evening lock cluster, STEP 8a (2026-11-26):
//   (1) THE FREE-TURN STANDING PASS (TurnSolver.cpp, FreeTurnTrueD0Scope): along a projected lock every opponent turn
//       is free, the round never advances, and the free-turn depth give-back turned a host's PASS 0 (the ranking that
//       always stands, never cut) into a search whose nesting was bounded only by TurnSeqCapped. Under a finite budget
//       pass 0 is now armed at the proportional ceiling and, when it trips, re-run with free turns counted as plies.
//   (2) THE FORCE OF VIGOR OWN-TARGET DOMINANCE FOLD (EnchantedEveningProvider::FovOwnTargetInert): our mana lands,
//       mana Auras, Exploration and Enchantress's Presence are withdrawn as targets (both windows); Sterling Grove,
//       Parallax Wave, Oblivion Ring, Enchanted Evening and Opalescence stay; a fetchland stays while a Calming Verse
//       is in our hand or library.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/SearchBudget.h"
#include "ai/TranspositionTable.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& L8Def(const std::string& name)
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
Card L8Card(const std::string& name, int number)
{
    Card c = L8Def(name).card;
    c.m_number = number;
    return c;
}
int L8Put(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = L8Card(name, number);
    p.controller_index = p.owner_index = controller;
    if (L8Def(name).params.fading_counters > 0) { p.fade_counters = L8Def(name).params.fading_counters; }
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
const Permanent& L8Perm(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    FAIL("no permanent ", number);
    return s.battlefield.front();
}
const EnchantedEveningProvider& L8Prov()
{
    static const EnchantedEveningProvider prov;
    return prov;
}
// Our turn 4 under Enchanted Evening (every permanent an enchantment: lands included), the EE provider.
GameState L8Board()
{
    L8Def("Forest");
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::Ending;
    s.step                = Step::MainPhase;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.deck_float_wild_exact = true;
    s.deck_has_type_statics = true;
    s.deck_has_eot_fov_pitch = true;
    s.m_provider = &L8Prov();
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L8Card("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L8Card("Plains", 100900 + k)); }
    L8Put(s, "Enchanted Evening", 51);
    return s;
}
bool PinsMention(const std::vector<std::string>& pins, int number)
{
    for (const std::string& pin : pins)
    {
        std::vector<int> nums;
        if (!DestroyUpToArtEnchPinTargets(pin, nums)) { continue; }
        if (std::find(nums.begin(), nums.end(), number) != nums.end()) { return true; }
    }
    return false;
}
} // namespace

TEST_CASE("EE lock step 8a: Force of Vigor own-target fold -- which of our permanents are withdrawn, which stay")
{
    GameState s = L8Board();
    L8Put(s, "Forest", 101);
    L8Put(s, "Temple Garden", 102);
    L8Put(s, "Windswept Heath", 103);
    L8Put(s, "Wild Growth", 104);
    s.battlefield.back().aura_attached_to = 101;
    L8Put(s, "Utopia Sprawl", 105);
    s.battlefield.back().aura_attached_to = 102;
    L8Put(s, "Exploration", 106);
    L8Put(s, "Enchantress's Presence", 107);
    L8Put(s, "Opalescence", 108);   // (kept: its static kills our lands under EE)
    L8Put(s, "Parallax Wave", 109);
    L8Put(s, "Oblivion Ring", 110);
    L8Put(s, "Plains", 700001, 1);   // an opponent land: an enchantment under EE, a target
    RefreshTypeStatics(s);
    const EnchantedEveningProvider& prov = L8Prov();
    // No Calming Verse in our hand or library: the fetchland is folded too.
    for (int n : { 101, 102, 103, 104, 105, 106, 107 })
    { INFO("folded " << n); CHECK(prov.FovOwnTargetInert(s, 0, L8Perm(s, n))); }
    for (int n : { 51, 108, 109, 110 })
    { INFO("kept " << n); CHECK_FALSE(prov.FovOwnTargetInert(s, 0, L8Perm(s, n))); }
    CHECK_FALSE(prov.FovOwnTargetInert(s, 0, L8Perm(s, 700001)));   // never an opponent permanent
    // The searched pins (both windows build them here): nothing folded appears, alone or in a pair; the kept own
    // targets and the opponent land do, and so does the opponent land paired with a kept own target.
    const std::vector<std::string> pins = DestroyUpToArtEnchSearchPins(s, 0, 2);
    for (int n : { 101, 102, 103, 104, 105, 106, 107 }) { INFO("pin " << n); CHECK_FALSE(PinsMention(pins, n)); }
    for (int n : { 51, 108, 109, 110, 700001 })       { INFO("pin " << n); CHECK(PinsMention(pins, n)); }
    CHECK(std::find(pins.begin(), pins.end(), DestroyUpToArtEnchPin({ 700001 })) != pins.end());
    CHECK(std::find(pins.begin(), pins.end(), DestroyUpToArtEnchPin({})) == pins.end());   // the empty set: withdrawn

    // A Calming Verse in our library: the fetchland is KEPT (an uncrackable untapped fetch is the one state where its
    // destruction changes Verse's clause 2); the mana lands stay folded (Verse's KEEP floats them).
    GameState v = s;
    v.players[0].library.push_back(L8Card("Calming Verse", 950));
    CHECK_FALSE(prov.FovOwnTargetInert(v, 0, L8Perm(v, 103)));
    CHECK(prov.FovOwnTargetInert(v, 0, L8Perm(v, 101)));
    GameState h = s;
    h.players[0].hand.push_back(L8Card("Calming Verse", 951));
    CHECK_FALSE(prov.FovOwnTargetInert(h, 0, L8Perm(h, 103)));
}

TEST_CASE("EE lock step 8a: Sterling Grove stays a target (ruling 17: its shroud blocks the Wave loop)")
{
    GameState s = L8Board();
    L8Put(s, "Sterling Grove", 120);
    L8Put(s, "Exploration", 121);   // shrouded by the Grove: not a legal target at all
    RefreshTypeStatics(s);
    CHECK_FALSE(L8Prov().FovOwnTargetInert(s, 0, L8Perm(s, 120)));
    const std::vector<std::string> pins = DestroyUpToArtEnchSearchPins(s, 0, 2);
    CHECK(PinsMention(pins, 120));
    CHECK_FALSE(PinsMention(pins, 121));
}

TEST_CASE("EE lock step 8a: the fold lives in the EE provider only -- the base provider folds nothing")
{
    GameState s = L8Board();
    L8Put(s, "Exploration", 106);
    RefreshTypeStatics(s);
    const GenericProvider base;
    CHECK_FALSE(base.FovOwnTargetInert(s, 0, L8Perm(s, 106)));
    s.m_provider = nullptr;   // the default route
    CHECK(PinsMention(DestroyUpToArtEnchSearchPins(s, 0, 2), 106));
}

// THE NON-TERMINATION CASE. A projected lock that never ends: Enchanted Evening out, no creature of ours (no
// Opalescence), the opponent's hand and library hold only lands -- each turn it plays one (an enchantment under EE, so
// a Force of Vigor target), holds no nonland card, and stays locked: every opponent turn is free and the round never
// advances, so no rollout can reach a cutoff before TurnSeqCapped. A Force of Vigor with mana and a green card to pitch
// is held, so every free turn's end step fans its menu. Before the standing pass the executor's crossing node (the
// d3 b10 hang, ho1 gi=9) ran pass 0 without any ceiling and nested one solve per free turn. Now pass 0 trips its
// proportional ceiling and is re-run as the standing pass: the call returns, having spent about the ceiling.
namespace
{
GameState L8LockedChain()
{
    L8Def("Forest");
    Decklist deck;
    for (int i = 0; i < 40; ++i) { deck.mainboard.push_back(L8Def("Forest").card); }
    GameState s = GoldFishRunner::SetupGame(deck, 8, 0);
    REQUIRE(s.opponent_plays_lands);
    s.players[1].hand.clear();
    s.opponent2_hand.clear();
    s.battlefield.clear();
    s.opponent_spawns = nullptr;
    while (!s.players[1].library.empty()) { s.players[1].library.erase(s.players[1].library.begin()); }
    for (int k = 0; k < 40; ++k) { s.players[1].library.push_back(L8Card("Plains", 100500 + k)); }
    s.players[0].hand.clear();
    s.active_player_index = 0;
    s.turn_number = 4;
    s.phase = Phase::Ending;
    s.step  = Step::MainPhase;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_free_turn = true;
    s.deck_float_wild_exact = true;
    s.deck_has_type_statics = true;
    s.deck_has_eot_fov_pitch = true;
    s.m_provider = &L8Prov();
    L8Put(s, "Enchanted Evening", 51);
    for (int k = 0; k < 4; ++k) { L8Put(s, "Forest", 101 + k); }
    L8Put(s, "Plains", 100400, 1);   // the opponent's land of this round (a target under EE)
    RefreshTypeStatics(s);
    s.players[0].hand.push_back(L8Card("Force of Vigor", 30));
    s.players[0].hand.push_back(L8Card("Wild Growth", 31));   // green: a pitch, and a cast
    return s;
}
} // namespace

TEST_CASE("EE lock step 8a: the executor's crossing node returns on an endless lock under a finite budget")
{
    GameState s = L8LockedChain();
    REQUIRE(TurnSolver::FaceUpPendingForTest(s));
    SearchBudget budget = SearchBudget::FromVirtualMs(1);
    const int pin = TurnSolver::ChooseRealCrossingFaceUp(s, 3, 6, &budget, true);
    CHECK(pin >= -2);
    // Pass 0 ran into the proportional ceiling (25 x the budget): the scenario does reach the unbounded shape ...
    CHECK(budget.Used() >= 25 * budget.Limit());
    // ... and the standing pass (free turns as plies, greedy beyond this turn) is cheap beside it.
    CHECK(budget.Used() < 40 * budget.Limit());
}

TEST_CASE("EE lock step 8a: the main-phase root solve returns on an endless lock under a finite budget")
{
    GameState s = L8LockedChain();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 0;
    s.players[0].hand.push_back(L8Card("Forest", 32));
    SearchBudget budget = SearchBudget::FromVirtualMs(1);
    TranspositionTable tt;
    const TurnSolver::Plan plan = TurnSolver::SolveWithLookahead(s, true, 3, 6, &budget, /*enforce_budget=*/true,
                                                                 /*second_main=*/true, &tt);
    (void)plan;
    CHECK(budget.Used() < 60 * budget.Limit());
}
