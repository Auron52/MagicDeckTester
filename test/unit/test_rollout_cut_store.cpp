// RC-1 (Maze's End rollout-cache drift, 2026-11-20): a computation that a BUDGET cut must not be stored as the
// budget-free answer. The charged greedy walk (MTG_SOLVE_CHARGE; Maze's End and Snow opt in) stops at its
// best-so-far once the rollout's budget is spent, and two caches stored that best-so-far as though it were the
// answer for the key:
//   * the greedy Solve memo (solvememo) served the cut plan to every later solve of the position in the decision,
//     unbudgeted ones included -- MTG_NOWIN_VERIFY's "SAME-STATE DRIFT" on 2HG d1 b1 g491;
//   * the leaf table stored the cut rollout's LATER win and served it -- MTG_LEAF_VERIFY's cached=8 fresh=7.
// Both stores now refuse when budgetbind::t_binds moved beneath them (MTG_SOLVE_MEMO_BIND_GUARD,
// MTG_LEAF_WIN_BIND_GUARD; default ON). Each case reproduces the stale hit with the guard's own probe: the cut must
// differ from the uncut answer (the precondition), and the later unbudgeted query must get the uncut answer.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/SearchBudget.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <string>

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

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// Turn 6, our pre-combat main, land drop spent, opponent at 4. Bloodghast's attack deals 2 a turn and Lightning
// Bolt (a Mountain untapped) deals 3: the uncut walk casts the Bolt and kills THIS turn, a walk cut before it
// considers anything passes and the attacks kill next turn. A library of Swamps keeps the future predictable.
GameState Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 4;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampires.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Mountain", 50);
    Put(s, "Bloodghast", 1);
    Hand(s, "Lightning Bolt", 10);
    return s;
}

constexpr int kMaxTurns = 8;

}   // namespace

TEST_CASE("RC-1: the greedy Solve memo does not serve a budget-cut walk's plan to a later uncut solve")
{
    EnsureCardsLoaded();
    TurnSolver::ClearPerGameCaches();
    const TurnSolver::CutMemoProbe r = TurnSolver::SolveMemoCutProbeForTest(Board(), /*is_pre_combat=*/true);
    REQUIRE(r.cut_differs);          // the cut really changed the plan (else the case proves nothing)
    CHECK(r.served_is_full);         // the later, unbudgeted solve gets the uncut plan
    CHECK_FALSE(r.served_is_cut);
}

TEST_CASE("RC-1: the leaf table does not store a budget-cut rollout's later win")
{
    EnsureCardsLoaded();
    TurnSolver::ClearPerGameCaches();
    const TurnSolver::CutLeafProbe r = TurnSolver::LeafWinCutProbeForTest(Board(), kMaxTurns);
    CHECK(r.fresh == 6);             // the uncut rollout casts the Bolt: a kill this turn
    REQUIRE(r.cut > r.fresh);        // the cut rollout wins later (the precondition)
    CHECK(r.cut <= kMaxTurns);       // ...and it is a WIN, i.e. a value the win table would store
    CHECK(r.served == r.fresh);      // the later unbudgeted query recomputes instead of being served the cut win
}
