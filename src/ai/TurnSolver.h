#pragma once
#include "../core/GameState.h"
#include "../cards/CardDatabase.h"
#include "SearchBudget.h"
#include <chrono>
#include <string>
#include <vector>

class TranspositionTable;  // per-decision SimulateToEnd memo (see TranspositionTable.h)

// Finds the optimal set of spells to cast in one main phase by exhaustive
// subset enumeration (O(2^|hand|), tractable for hand sizes up to ~15).
//
// Evaluation accounts for tempo: creatures are valued at power * expected
// remaining attacks, not just immediate damage. This prevents the solver
// from always preferring burn over board development.
//
// Sacrifice-land spells are always placed last in the execution order so
// that other spells have already tapped their lands before the sacrifice
// fires, minimising the real cost of the additional cost.
class TurnSolver
{
public:
    struct Plan
    {
        // Spells to execute in order: regular spells first, sacrifice-land last.
        std::vector<std::string> spells;
        std::vector<std::string> sacrifice;
        // Creatures to deploy via Aether Vial (zero mana cost, execute before spells).
        std::vector<std::string> vial_activations;
        int  value          = -1;   // -1 = nothing castable
        bool wins_this_turn = false;

        bool empty() const
        {
            return spells.empty() && sacrifice.empty() && vial_activations.empty();
        }
    };

    // Returns the highest-value feasible plan for one main phase.
    // Uses a static evaluation function (no lookahead).
    static Plan Solve(const GameState& state, bool is_pre_combat);

    // Enable per-pass per-candidate trace output for top-level T1 decisions.
    static void SetTraceSolve(bool enable);
    static bool GetTraceSolve();

    // Returns the plan that leads to the earliest win, evaluated by simulating
    // the rest of the game for each candidate play at this turn.
    // depth=0 falls back to Solve.  depth=1 simulates one turn ahead using Solve
    // for all subsequent decisions; depth=2 uses depth=1 for subsequent decisions;
    // and so on.
    //
    // budget: deterministic work budget (see SearchBudget). All rollout work is
    // counted against it; nullptr means unlimited. enforce_budget governs whether
    // THIS invocation may stop iterative deepening when the budget runs out:
    //   - true  (top-level decision): applies the start-gate / overrun-guard and
    //            commits the deepest fully-completed pass.
    //   - false (rollout sub-search):  runs every pass to completion regardless,
    //            only consuming from the shared budget (preserves rollout
    //            fidelity, mirroring the old time_point::max() deadline).
    //
    // second_main: when true, the simulation plays a post-combat (second) main
    // phase each turn (greedy in the rollout), and a top-level is_pre_combat=false
    // call is treated as a real second-main decision (no combat is re-simulated).
    // Off for most decks; on only for ones whose combat enables second-main plays
    // (spectacle unlocked by combat damage, lands untapped in combat). See
    // AIEngine::SetSearchPostCombat.
    //
    // tt: per-decision transposition table memoizing SimulateToEnd. The enforcing
    // top-level call creates one when none is supplied and threads it through the
    // whole recursion; rollout sub-searches forward the table they were given.
    static Plan SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                   int depth, int max_turns = 20,
                                   SearchBudget* budget = nullptr,
                                   bool enforce_budget = true,
                                   bool second_main = false,
                                   TranspositionTable* tt = nullptr);
};
