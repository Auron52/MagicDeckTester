#pragma once
#include "../core/GameState.h"
#include "../cards/CardDatabase.h"
#include <chrono>
#include <string>
#include <vector>

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
        int  value          = -1;   // -1 = nothing castable
        bool wins_this_turn = false;

        bool empty() const { return spells.empty() && sacrifice.empty(); }
    };

    // Returns the highest-value feasible plan for one main phase.
    // Uses a static evaluation function (no lookahead).
    static Plan Solve(const GameState& state, bool is_pre_combat);

    // Returns the plan that leads to the earliest win, evaluated by simulating
    // the rest of the game for each candidate play at this turn.
    // depth=0 falls back to Solve.  depth=1 simulates one turn ahead using Solve
    // for all subsequent decisions; depth=2 uses depth=1 for subsequent decisions;
    // and so on.
    // deadline: if now() >= deadline, the search returns the best plan found so
    // far rather than evaluating remaining candidates.  Pass time_point::max()
    // (the default) for no timeout.
    static Plan SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                   int depth, int max_turns = 20,
                                   std::chrono::steady_clock::time_point deadline =
                                       std::chrono::steady_clock::time_point::max());
};
