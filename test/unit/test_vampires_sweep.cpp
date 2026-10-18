// Unit tests for the Vampires claude-play sweep's OUTLET / ACTIVATION fixes (2026-10-18; ledger "Sweep
// fixes"):
//   E. every discard-cost outlet -- pinned or not -- must have a card to discard at its turn in the
//      EXECUTED trailing order (subset rule 8);
//   A/B. the convoke body books (subset rule 7), driven directly with hand-built selections;
//   G. two identical untapped Voldaren Estates are one Blood source (the canonical-prefix fold);
//   D. the display recorder names the bodies a convoke cast really tapped;
//   M. Voldaren Estate's Blood cost variant books exactly the Vampires that arrive AHEAD of it in the
//      executed trailing order (subset rule 3; defect M), and TrailingRunsBefore reads that order.
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

// ---- M. Voldaren Estate's Blood books the arrivals AHEAD of it in the executed order (defect M) ---------

namespace
{
Action Blood(int src, int k, bool top)
{
    Action a;
    a.kind = Action::Kind::ActivatePermAbility;
    a.card_name = std::string("Voldaren Estate");
    a.def = &Def("Voldaren Estate");
    a.sac_source_id = src;
    a.ability_mode = Action::AbilityMode::TapBlood;
    a.chosen_x = 1;
    a.vamp_casts_needed = static_cast<std::int8_t>(top ? k + 16 : k);
    return a;
}

Action TokenTap(int src)   // Bloodline Keeper's "{T}: Create a 2/2 black Vampire" (the enumerator sets no def)
{
    Action a;
    a.kind = Action::Kind::TapForTokenPay;
    a.card_name = std::string("Bloodline Keeper");
    a.sac_source_id = src;
    return a;
}

Action Steal(int src, int target)
{
    Action a;
    a.kind = Action::Kind::ActivatePermAbility;
    a.card_name = std::string("Captivating Vampire");
    a.def = &Def("Captivating Vampire");
    a.sac_source_id = src;
    a.ability_mode = Action::AbilityMode::TapSubtypeSteal;
    a.enchant_target = target;
    a.chosen_x = 1;
    return a;
}

Action Transform(int src)
{
    Action a;
    a.kind = Action::Kind::ActivatePermAbility;
    a.card_name = std::string("Bloodline Keeper");
    a.def = &Def("Bloodline Keeper");
    a.sac_source_id = src;
    a.ability_mode = Action::AbilityMode::TransformSelf;
    a.chosen_x = 1;
    return a;
}

Action PitFighter(int src, bool same_line)
{
    Action a;
    a.kind = Action::Kind::SacCreatureOutlet;
    a.card_name = std::string("Falkenrath Pit Fighter");
    a.def = &Def("Falkenrath Pit Fighter");
    a.sac_source_id = src;
    if (same_line) { a.sac_victim_id = kSameLineSacVictim; }
    return a;
}

Action WithMadnessCast(Action a, const std::string& card) { a.discard_name = card; a.madness_choice = 1; return a; }

bool Rule3Rejects(const GameState& s, const std::vector<Action>& cands, const std::vector<int>& sel)
{ return TurnSolver::VampSweepSubsetRuleRejects(s, cands, sel, 3); }
}   // namespace

TEST_CASE("Sweep M: rule 3 books exactly the Vampires that arrive AHEAD of the Blood -- casts and token taps, never what runs after it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 11);
    Put(s, "Stromkirk Condemned", 6);
    Put(s, "Captivating Vampire", 8);
    Put(s, "Falkenrath Pit Fighter", 9);
    Put(s, "Voldaren Estate", 58);
    Put(s, "Blood Token", 70);
    {
        Permanent opp;
        opp.card = Def("Vampire of the Dire Moon").card;
        opp.card.m_number = 500;
        opp.controller_index = 1;
        opp.owner_index = 1;
        s.battlefield.push_back(opp);
    }
    Hand(s, "Markov Baron", 30);
    Hand(s, "Vampire of the Dire Moon", 31);
    // cands: 0 = Blood k0, 1 = Blood k1, 2 = Blood "k2 or more", 3 = a Vampire cast, 4 = Keeper's token,
    // 5 = Condemned's pump madness-casting the Baron, 6 = the Blood crack madness-casting it, 7 = the
    // steal, 8 = Pit Fighter (sacrifices a Vampire), 9 = Keeper's transform.
    const std::vector<Action> cands = {
        Blood(58, 0, false), Blood(58, 1, false), Blood(58, 2, true),
        Cast("Vampire of the Dire Moon", 1), TokenTap(11),
        WithMadnessCast(Pump(6), "Markov Baron"), WithMadnessCast(Crack(70), "Markov Baron"),
        Steal(8, 500), PitFighter(9, false), Transform(11) };
    // Nothing added: k0 only.
    CHECK_FALSE(Rule3Rejects(s, cands, {0}));
    CHECK(Rule3Rejects(s, cands, {1}));
    // The cast section runs before the trailing pass: one arrival.
    CHECK(Rule3Rejects(s, cands, {0, 3}));
    CHECK_FALSE(Rule3Rejects(s, cands, {1, 3}));
    // Keeper's token (rank 0) runs AHEAD of the Blood (rank 1) -- whichever comes first in `sel` (the
    // odometer lays the Blood's group before the independent token tap): THE DEFECT, now booked.
    CHECK(Rule3Rejects(s, cands, {0, 4}));
    CHECK_FALSE(Rule3Rejects(s, cands, {1, 4}));
    CHECK_FALSE(Rule3Rejects(s, cands, {4, 1}));
    CHECK(Rule3Rejects(s, cands, {1, 3, 4}));
    CHECK_FALSE(Rule3Rejects(s, cands, {2, 3, 4}));
    // ...and with a count band in the pass (the transform), the token is still band 0 ahead of the Blood.
    CHECK_FALSE(Rule3Rejects(s, cands, {1, 4, 9}));
    CHECK(Rule3Rejects(s, cands, {0, 4, 9}));
    // The madness casts run AFTER the Blood (the pump's rank 2, the crack's 3): they do not lower its
    // cost, so booking them would UNDER-price it. Not counted.
    CHECK_FALSE(Rule3Rejects(s, cands, {0, 5}));
    CHECK(Rule3Rejects(s, cands, {1, 5}));
    CHECK_FALSE(Rule3Rejects(s, cands, {0, 6}));
    CHECK(Rule3Rejects(s, cands, {1, 6}));
    // The steal (count band 1) runs after the band-0 Blood: not counted either.
    CHECK_FALSE(Rule3Rejects(s, cands, {0, 7}));
    CHECK(Rule3Rejects(s, cands, {1, 7}));
    CHECK_FALSE(Rule3Rejects(s, cands, {1, 4, 7}));
    // Pit Fighter's sacrifice (band 3 / rank 4) runs after the Blood: the cast still books k1.
    CHECK_FALSE(Rule3Rejects(s, cands, {1, 3, 8}));
}

TEST_CASE("Sweep M: TurnSolver::TrailingRunsBefore reads exactly the order both trailing passes sort into")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 11);
    Put(s, "Stromkirk Condemned", 6);
    Put(s, "Captivating Vampire", 8);
    Put(s, "Falkenrath Pit Fighter", 9);
    Put(s, "Voldaren Estate", 58);
    Put(s, "Voldaren Estate", 59);
    Put(s, "Blood Token", 70);
    for (const bool with_bands : {false, true})
    {
        std::vector<Action> acts = {
            PitFighter(9, true), Blood(58, 0, false), WithMadnessCast(Pump(6), "Markov Baron"), TokenTap(11),
            Cast("Vampire of the Dire Moon", 0), Crack(70), PitFighter(9, false), Blood(59, 1, true) };
        if (with_bands) { acts.push_back(Steal(8, 500)); acts.insert(acts.begin() + 2, Transform(11)); }
        REQUIRE(TurnSolver::TrailingHasCountBands(acts) == with_bands);
        std::vector<Action> sorted = acts;
        for (std::size_t i = 0; i < sorted.size(); ++i) { sorted[i].eval = static_cast<int>(i); }
        TurnSolver::OrderTrailingActivations(s, sorted);   // both passes: the sort, then the same-line move
        TurnSolver::DeferSameLineSacs(sorted);
        std::vector<std::size_t> pos(acts.size());
        for (std::size_t k = 0; k < sorted.size(); ++k) { pos[static_cast<std::size_t>(sorted[k].eval)] = k; }
        for (std::size_t i = 0; i < acts.size(); ++i)
        {
            if (!TurnSolver::IsTrailingActivation(acts[i].kind)) { continue; }
            for (std::size_t j = 0; j < acts.size(); ++j)
            {
                if (i == j || !TurnSolver::IsTrailingActivation(acts[j].kind)) { continue; }
                INFO("bands=" << with_bands << " i=" << i << " j=" << j);
                CHECK(TurnSolver::TrailingRunsBefore(s, acts[i], i, acts[j], j, with_bands) == (pos[i] < pos[j]));
            }
        }
    }
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
