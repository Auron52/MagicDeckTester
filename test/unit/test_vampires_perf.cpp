// Unit tests for the Vampires Stage 5f search-cost folds (2026-10-18; docs/design/vampires-search-cost.md):
//   * MTG_VAMP_MADNESS_COLOR_FOLD -- a madness CAST variant the turn's optimistic pool cannot fund in
//     colour is not emitted (it would resolve as the decline it duplicates);
//   * MTG_VAMP_OUTLET_NOT_GROWTH -- a DISCARD-cost sac outlet (Falkenrath Pit Fighter) no longer disarms
//     OptimisticTurnMana's ceiling;
//   * MTG_VAMP_M2_PUMP_DISCARD_FOLD -- subset rule 9, the post-combat discard-pump dominance fold, and its
//     exemptions (madness cast, Bloodghast, hellbent, the cleanup's seven, a draw in the selection).
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
    s.players[0].lands_played_this_turn = 1;   // land drop spent
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    s.deck_has_madness = true;
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

// Stromkirk Condemned's discard pump, PINNED to `pin` with madness answer `mad` (-1 = no fan).
Action PinnedPump(int src, const std::string& pin, int mad = -1)
{
    Action a;
    a.kind = Action::Kind::ActivatePump;
    a.card_name = std::string("Stromkirk Condemned");
    a.def = &Def("Stromkirk Condemned");
    a.sac_source_id = src;
    a.gy_exile_mode = 4;
    a.chosen_x = 1;
    a.discard_name = pin;
    a.madness_choice = static_cast<std::int8_t>(mad);
    return a;
}

Action Crack(int src)
{
    Action a;
    a.kind = Action::Kind::ActivatePermAbility;
    a.card_name = std::string("Blood Token");
    a.def = &Def("Blood Token");
    a.sac_source_id = src;
    a.ability_mode = Action::AbilityMode::BloodDraw;
    a.chosen_x = 1;
    a.cost.generic = 1;
    a.discard_name = std::string("Legion Lieutenant");
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

// The madness answers the enumeration offers for Condemned discarding `pin`.
std::set<int> PumpChoices(const GameState& s, const std::string& pin, bool pre = true)
{
    std::set<int> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, pre))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == 4 && a.discard_name.str() == pin)
            { out.insert(a.madness_choice); }
        }
    }
    return out;
}

struct ArmScope   // pin one heurarm slot for the scope
{
    heurarm::Slot s;
    explicit ArmScope(heurarm::Slot slot, int v) : s(slot) { heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmScope() { heurarm::t_arm[s] = -1; }
};

}   // namespace

TEST_CASE("Madness colour fold: a Swamp-only board offers no {R} madness cast; a red source restores it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Falkenrath Gorger", 2);              // grants madness to every Vampire card we own
    Put(s, "Swamp", 50);
    Hand(s, "Falkenrath Pit Fighter", 10);       // {R}: madness through the Gorger -- and a sac outlet
    Hand(s, "Legion Lieutenant", 11);
    {
        const std::set<int> c = PumpChoices(s, "Falkenrath Pit Fighter");
        CHECK(c.count(0) == 1);   // the decline is always offered
        CHECK(c.count(1) == 0);   // the {R} cast no line can fund is folded into it
    }
    {
        ArmScope off(heurarm::VAMP_MADNESS_COLOR_FOLD, 0);   // =0: the total-mana gate only -> offered again
        CHECK(PumpChoices(s, "Falkenrath Pit Fighter").count(1) == 1);
    }
    Put(s, "Blackcleave Cliffs", 51);            // a red source: the cast is real
    const std::set<int> c = PumpChoices(s, "Falkenrath Pit Fighter");
    CHECK(c.count(0) == 1);
    CHECK(c.count(1) == 1);
}

TEST_CASE("Outlet not growth: Falkenrath Pit Fighter no longer disarms the mana ceiling")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Falkenrath Gorger", 2);
    Put(s, "Falkenrath Pit Fighter", 3);         // a sac_creature_outlet ON THE BATTLEFIELD
    Put(s, "Swamp", 50);
    Hand(s, "Falkenrath Gorger", 10);            // {R} through the battlefield Gorger's grant
    Hand(s, "Legion Lieutenant", 11);
    // The ceiling is armed with a draw outlet in play, so the colour fold sees the lone Swamp.
    CHECK(PumpChoices(s, "Falkenrath Gorger").count(1) == 0);
    {
        // =0: every sac outlet is growth -> no ceiling -> no colour bound -> the cast variant (a decline
        // twin: the subset walk's total-mana check passes on {R}-as-generic) is back.
        ArmScope off(heurarm::VAMP_OUTLET_NOT_GROWTH, 0);
        CHECK(PumpChoices(s, "Falkenrath Gorger").count(1) == 1);
    }
}

TEST_CASE("Rule 9: a post-combat discard pump that only discards is dominated -- and its exemptions")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.phase = Phase::PostCombatMain;
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51);
    Hand(s, "Swamp", 10);
    Hand(s, "Legion Lieutenant", 11);
    Hand(s, "Vampire of the Dire Moon", 12);
    Hand(s, "Bloodghast", 13);
    const std::vector<Action> cands = {
        PinnedPump(1, "Swamp"),                      // 0: a plain discard
        PinnedPump(1, "Markov Baron", 0),            // 1: a madness DECLINE (still only a discard)
        PinnedPump(1, "Vampire of the Dire Moon", 1),// 2: a madness CAST -- never folded
        PinnedPump(1, "Bloodghast"),                 // 3: comes back on landfall -- never folded
        Cast("Legion Lieutenant", 1),                // 4
        Crack(2),                                    // 5: a draw in the selection
    };
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0 }, 9));
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 1 }, 9));
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 2 }, 9));
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 3 }, 9));
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 4, 0 }, 9));
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0, 5 }, 9));   // the crack draws
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 4 }, 9));      // no pump at all
    {
        ArmScope off(heurarm::VAMP_M2_PUMP_DISCARD_FOLD, 0);
        CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0 }, 9));
    }
}

TEST_CASE("Rule 9: hellbent and the cleanup's seven keep the discard a real choice")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.phase = Phase::PostCombatMain;
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Swamp", 50);
    Hand(s, "Swamp", 10);                            // the last card
    const std::vector<Action> cands = { PinnedPump(1, "Swamp") };
    // No hellbent reader: an empty hand buys nothing -- dominated.
    CHECK(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0 }, 9));
    // Asylum Visitor on the battlefield: emptying the hand draws a card at our upkeep -- kept.
    Put(s, "Asylum Visitor", 2);
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(s, cands, { 0 }, 9));
    // Nine cards: the cleanup would discard down to seven, so WHICH card goes is a decision -- kept.
    GameState big = Fresh();
    big.phase = Phase::PostCombatMain;
    Put(big, "Stromkirk Condemned", 1);
    for (int k = 0; k < 9; ++k) { Hand(big, "Swamp", 20 + k); }
    CHECK_FALSE(TurnSolver::VampSweepSubsetRuleRejects(big, cands, { 0 }, 9));
}

TEST_CASE("Rule 9 in the walk: second-main discard-only pumps are not enumerated; main 1 keeps them all")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Legion Lieutenant", 2);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51);
    Hand(s, "Swamp", 10);
    Hand(s, "Captivating Vampire", 11);
    Hand(s, "Bloodghast", 12);
    // Main 1: the pump reaches combat -- every discard pin is a searched variant.
    const std::set<int> pre_swamp = PumpChoices(s, "Swamp", /*pre=*/true);
    CHECK(!pre_swamp.empty());
    // Main 2: the plain discards are dominated by holding the card; Bloodghast's survives.
    s.phase = Phase::PostCombatMain;
    CHECK(PumpChoices(s, "Swamp", /*pre=*/false).empty());
    CHECK(PumpChoices(s, "Captivating Vampire", /*pre=*/false).empty());
    CHECK(!PumpChoices(s, "Bloodghast", /*pre=*/false).empty());
    {
        ArmScope off(heurarm::VAMP_M2_PUMP_DISCARD_FOLD, 0);
        CHECK(!PumpChoices(s, "Swamp", /*pre=*/false).empty());
    }
}
