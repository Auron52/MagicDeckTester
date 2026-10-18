// Unit tests for the Vampires claude-play sweep's OUTLET / ACTIVATION fixes (2026-10-18; ledger "Sweep
// fixes"):
//   E. every discard-cost outlet -- pinned or not -- must have a card to discard at its turn in the
//      EXECUTED trailing order (subset rule 8);
//   A/B. the convoke body books (subset rule 7), driven directly with hand-built selections;
//   G. two identical untapped Voldaren Estates are one Blood source (the canonical-prefix fold);
//   D. the display recorder names the bodies a convoke cast really tapped.
// The human-play surfaces (the unpinned outlet entries, the transform in human menus, the all-or-nothing
// probe, the menu's listing order) cannot be reached from a unit test -- HumanPlayActive() is a process
// static -- so their predicates are driven here directly and the menus are covered by the sweep repros
// recorded in the ledger.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

int Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

Action Pump(int src)       // Stromkirk Condemned's discard pump, UNPINNED (the human-play entry)
{
    Action a;
    a.kind = Action::Kind::ActivatePump;
    a.card_name = std::string("Stromkirk Condemned");
    a.def = &Def("Stromkirk Condemned");
    a.sac_source_id = src;
    a.gy_exile_mode = 4;
    a.chosen_x = 1;
    return a;
}

Action Crack(int src)      // the Blood token's "{1}, {T}, Discard a card, Sacrifice: Draw a card", unpinned
{
    Action a;
    a.kind = Action::Kind::ActivatePermAbility;
    a.card_name = std::string("Blood Token");
    a.def = &Def("Blood Token");
    a.sac_source_id = src;
    a.ability_mode = Action::AbilityMode::BloodDraw;
    a.chosen_x = 1;
    a.cost.generic = 1;
    return a;
}

Action Cast(const std::string& name, int hand_index)
{
    Action a;
    a.kind = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def = &Def(name);
    a.hand_index = hand_index;
    a.cost = Def(name).card.m_mana_cost;
    return a;
}

}   // namespace

// ---- E. discard outlets in the executed order ------------------------------------------------------

TEST_CASE("Sweep E: an unpinned outlet needs a card after the plan's own casts, in the executed order")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Blood Token", 2);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Captivating Vampire", 10);                 // ONE card in hand
    const std::vector<Action> cands = { Pump(1), Crack(2), Cast("Captivating Vampire", 0) };
    // The pump alone, the crack alone: one card each -- fine.
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0 }, 8));
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 1 }, 8));
    // Seed 91007: pump + crack with one card. The pass runs Condemned before the crack (the provider's
    // ActivationOrderRank), so the pump eats the only card and the crack cannot discard -- rejected.
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0, 1 }, 8));
    // Seeds 91009 / 91011 / 91013: the plan's own cast empties the hand -- the pump has nothing left.
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 2, 0 }, 8));
    // A second card makes both lines real.
    Hand(s, "Swamp", 11);
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0, 1 }, 8));
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 2, 0 }, 8));
    // The executed order is the one both trailing passes realise: Condemned before the crack.
    std::vector<Action> pass = { Crack(2), Pump(1) };
    TurnSolver::OrderTrailingActivations(s, pass);
    CHECK(pass.front().kind == Action::Kind::ActivatePump);
}

// ---- A / B. the convoke body books -----------------------------------------------------------------

TEST_CASE("Sweep A/B: subset rule 7 -- bodies are counted once; plan bodies need their arrivals")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);
    Put(s, "Stromkirk Condemned", 2, /*sick=*/true);
    Put(s, "Legion Lieutenant", 3, /*sick=*/true);
    Hand(s, "Markov Baron", 10);
    Hand(s, "Markov Baron", 11);
    Hand(s, "Vampire of the Dire Moon", 12);
    Action b1 = Cast("Markov Baron", 0);  b1.convoke_green = 3; b1.cost = ManaCost{};
    Action b2 = Cast("Markov Baron", 1);  b2.convoke_green = 3; b2.cost = ManaCost{};
    Action b3 = Cast("Markov Baron", 1);  b3.convoke_green = 1; b3.convoke_plan_pip = 1; b3.cost.generic = 2;
    Action moon = Cast("Vampire of the Dire Moon", 2);
    Action b4 = Cast("Markov Baron", 0);  b4.convoke_green = 1; b4.convoke_plan_pip = 1; b4.cost.generic = 2;
    const std::vector<Action> cands = { b1, b2, b3, moon, b4 };
    // Seed 91011: both Barons counting the same three bodies -- six claimed, at most four exist.
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0, 1 }, 7));
    // Baron A takes the three bodies, Baron B convokes with Baron A (a same-plan arrival): four.
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0, 2 }, 7));
    // A plan-body arm with no arrival in the selection is a phantom.
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 4 }, 7));
    // ...and with the Dire Moon cast first it is real.
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 3, 4 }, 7));
    // Two plan-body arms leaning on each other alone cannot both be served (the last convoke cast
    // serves nobody).
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 2, 4 }, 7));
}

// ---- G. identical Estates ---------------------------------------------------------------------------

TEST_CASE("Sweep G: two identical untapped Voldaren Estates are ONE Blood source in the search")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Voldaren Estate", 58);
    Put(s, "Voldaren Estate", 59);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52); Put(s, "Swamp", 53);
    Put(s, "Vampire of the Dire Moon", 1);
    Put(s, "Stromkirk Condemned", 2);
    Put(s, "Legion Lieutenant", 3);
    // The canonical-prefix fold applies on the greedy walk always and on the search's own walk under
    // the deck opt-in (FOLD_SEARCH_ODO); pin the arm on so the enumerator test reads the fold. The fix
    // is that the Estates' TapBlood actions carry an equiv_tag at all (before: never folded anywhere).
    heurarm::t_arm[heurarm::FOLD_SEARCH_ODO] = 1;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    heurarm::t_arm[heurarm::FOLD_SEARCH_ODO] = -1;
    std::set<int> single_blood_src;
    int single = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        std::vector<int> srcs;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapBlood) { srcs.push_back(a.sac_source_id); } }
        if (srcs.size() != 1) { continue; }
        ++single;
        single_blood_src.insert(srcs.front());
    }
    CHECK(single > 0);
    CHECK(single_blood_src.size() == 1);   // was {58, 59}: the same line twice
}

// ---- D. the display recorder -----------------------------------------------------------------------

TEST_CASE("Sweep D: the convoke recorder names the bodies a cast really tapped (probe apply)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Vampire of the Dire Moon", 10);
    Hand(s, "Markov Baron", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int checked = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        bool arm = false;
        for (const Action& a : p.actions) { if (a.convoke_plan_pip > 0) { arm = true; } }
        if (!arm) { continue; }
        std::vector<ConvokeTapRecord> recs;
        g_convoke_tap_record = &recs;
        g_convoke_record_depth = 1;
        GameState c = s;
        TurnSolver::ApplyPlan(c, p, true);
        g_convoke_tap_record = nullptr;
        REQUIRE(recs.size() == 1);
        CHECK(recs[0].spell == "Markov Baron");
        REQUIRE(recs[0].bodies.size() == 1);
        CHECK(recs[0].bodies[0] == "Vampire of the Dire Moon #10");
        ++checked;
    }
    CHECK(checked > 0);
}
