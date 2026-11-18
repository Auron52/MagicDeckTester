// Unit tests for the ROLLOUT's direct-damage creature kills (design damage-reflection-pipeline.md R7).
//
// ApplyPlanDirect had two kill paths of its own that the executor does not share:
//   * the creature-burn branch (Searing Blood / Searing Blaze / Skred): `damage >= EffectiveToughness()`
//     then a bare erase -- no indestructible test, no graveyard, no OnCreatureDies;
//   * the human any-target branch (Lightning Bolt et al. under g_play_target_chooser): an inline SBA loop
//     with LethalToughness but no indestructible test and no OnCreatureDies.
// The executor marks the damage and lets GameEngine::CheckStateBasedActions kill: indestructible survives
// lethal damage (CR 704.5g), the card goes to a graveyard, OnCreatureDies fires (morbid, persist, death
// watchers). Both rollout paths now use the executor's shared SBA death (CounterPingSbaDead +
// CounterPingKillAt / SbaKillDeadCreatures). Each case below failed before that change.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameLogger.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

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
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Mountain").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
        c.m_number = 950 + k;
        s.players[1].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, int controller, int owner)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = owner;
    s.battlefield.push_back(p);
    return number;
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool InGraveyard(const GameState& s, int player, int number)
{
    for (const Card& c : s.players[player].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

Action CastAct(const std::string& name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &Def(name);
    a.hand_index     = -1;
    a.cost           = Def(name).card.m_mana_cost;
    a.is_noncreature = !Def(name).card.IsCreature();
    return a;
}

TurnSolver::Plan PlanOf(const std::vector<Action>& acts)
{
    TurnSolver::Plan p;
    p.actions = acts;
    return p;
}

void ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// The executor world: commit the plan, run the real TakeTurn, then the real SBA.
GameState Execute(const GameState& s, const TurnSolver::Plan& line)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAll);
    GameEngine ge(eng);
    MtgTestSeam::GeCheckSba(ge, exec);
    return exec;
}

// Board: two Mountains for us, `burn` in hand.
GameState BurnBoard(const std::string& burn)
{
    GameState s = Fresh();
    Put(s, "Mountain", 1, 0, 0);
    Put(s, "Mountain", 2, 0, 0);
    Hand(s, burn, 10);
    return s;
}

}   // namespace

TEST_CASE("R7 creature burn (rollout): an INDESTRUCTIBLE creature survives lethal damage, as in the executor")
{
    EnsureCardsLoaded();
    GameState s = BurnBoard("Searing Blaze");
    const int elf = Put(s, "Llanowar Elves", 40, 1, 1);
    s.battlefield.back().temp_indestructible = true;
    const TurnSolver::Plan line = PlanOf({ CastAct("Searing Blaze") });

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    // The spell resolved (Fresh() has played a land, so this is the landfall 3, not 1 -- either way lethal to a 1/1).
    REQUIRE(roll.players[1].life == s.players[1].life - 3);
    // Before the fix the rollout erased it (3 damage >= toughness 1, no indestructible test).
    REQUIRE(FindNum(roll, elf) != nullptr);
    CHECK(FindNum(roll, elf)->damage == 3);   // landfall 3 marked, survived
    CHECK_FALSE(InGraveyard(roll, 1, elf));

    const GameState exec = Execute(s, line);
    CHECK(FindNum(exec, elf) != nullptr);
    CHECK(exec.players[1].life == roll.players[1].life);
}

TEST_CASE("R7 creature burn (rollout): a killed creature goes to the graveyard and its death fires (morbid)")
{
    EnsureCardsLoaded();
    GameState s = BurnBoard("Searing Blood");
    const int elf = Put(s, "Llanowar Elves", 40, 1, 1);
    const TurnSolver::Plan line = PlanOf({ CastAct("Searing Blood") });

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    CHECK(FindNum(roll, elf) == nullptr);
    CHECK(roll.players[1].life == s.players[1].life - 3);   // the Blood's death rider
    // Before the fix: erased with no graveyard push and no OnCreatureDies.
    CHECK(InGraveyard(roll, 1, elf));
    CHECK(roll.creature_died_this_turn);

    const GameState exec = Execute(s, line);
    CHECK(FindNum(exec, elf) == nullptr);
    CHECK(InGraveyard(exec, 1, elf));
    CHECK(exec.creature_died_this_turn);
    CHECK(exec.players[1].life == roll.players[1].life);
}

TEST_CASE("R7 creature burn (rollout): a PERSIST creature killed by burn returns (its death trigger fires)")
{
    EnsureCardsLoaded();
    GameState s = BurnBoard("Searing Blood");
    const int finks = Put(s, "Kitchen Finks", 40, 1, 1);   // 3/2: Searing Blood's 2 kills it
    const TurnSolver::Plan line = PlanOf({ CastAct("Searing Blood") });

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    const GameState exec = Execute(s, line);

    // The executor: it dies, persist returns it with a -1/-1 counter under its owner's control.
    int exec_finks = 0;
    for (const Permanent& p : exec.battlefield)
    { if (p.card.m_name.str() == "Kitchen Finks" && p.controller_index == 1) { ++exec_finks; } }
    REQUIRE(exec_finks == 1);
    // Before the fix the rollout's erase skipped OnCreatureDies, so persist never returned it.
    int roll_finks = 0;
    for (const Permanent& p : roll.battlefield)
    {
        if (p.card.m_name.str() != "Kitchen Finks" || p.controller_index != 1) { continue; }
        ++roll_finks;
        CHECK(p.damage == 0);
        CHECK(MinusCountersOn(p) == 1);
    }
    CHECK(roll_finks == 1);
    CHECK((FindNum(roll, finks) == nullptr || FindNum(roll, finks)->damage == 0));
    CHECK(roll.players[1].life == exec.players[1].life);
}

TEST_CASE("R7 any-target burn (human rollout): indestructible survives; a killed creature fires its death")
{
    EnsureCardsLoaded();
    GameState s = BurnBoard("Lightning Bolt");
    const int shield = Put(s, "Llanowar Elves", 40, 0, 0);
    s.battlefield.back().temp_indestructible = true;
    const int victim = Put(s, "Llanowar Elves", 41, 1, 1);
    const TurnSolver::Plan line = PlanOf({ CastAct("Lightning Bolt") });

    // The human picks the target (the board click): first our indestructible Elf, then the opponent's.
    for (const int pick : { shield, victim })
    {
        TargetChooser chooser = [pick](const GameState& st, const CardDefinition&, int, int, int,
                                       const std::vector<ChosenTarget>&)
        {
            for (int i = 0; i < static_cast<int>(st.battlefield.size()); ++i)
            { if (st.battlefield[i].card.m_number == pick) { return std::vector<ChosenTarget>{ { 1, i, 0 } }; } }
            return std::vector<ChosenTarget>{};
        };
        GameState roll = s;
        g_play_target_chooser = &chooser;
        TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
        g_play_target_chooser = nullptr;
        REQUIRE(roll.players[1].life == s.players[1].life);   // the Bolt went to the creature
        if (pick == shield)
        {
            // Before the fix the inline SBA loop killed it (3 >= 1, no indestructible test).
            REQUIRE(FindNum(roll, shield) != nullptr);
            CHECK(FindNum(roll, shield)->damage == 3);
            CHECK_FALSE(roll.creature_died_this_turn);
        }
        else
        {
            CHECK(FindNum(roll, victim) == nullptr);
            CHECK(InGraveyard(roll, 1, victim));
            // Before the fix the loop skipped OnCreatureDies.
            CHECK(roll.creature_died_this_turn);
        }
    }
}
