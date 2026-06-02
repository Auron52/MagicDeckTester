#pragma once
#include "../core/GameState.h"
#include "../cards/CardDatabase.h"
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
    // Expected remaining attacks is estimated from the opponent's current life
    // total so creature value scales down correctly in the late game.
    static Plan Solve(const GameState& state, bool is_pre_combat);
};
