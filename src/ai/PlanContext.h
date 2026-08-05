#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct Action;

// PLAN CONTEXT: what else this turn's plan is going to do, visible to a DecisionProvider while it
// is being asked about ONE action inside that plan.
//
// Why it exists. A provider is handed a GameState, which describes everything that has ALREADY
// happened -- and for a decision made mid-plan that is only half the information. The other half is
// the plan itself: which land is being played, what else gets cast this turn, and how much mana is
// left afterwards. GoblinsProvider::TutorCandidates currently hand-rolls guesses at exactly that
// (entering_fodder, buff_targets' hand component, hand_has_play, haste_avail, mana_now/mana_next),
// and each of those is a tuned stand-in for something the plan already knows exactly.
//
// This also fixes the state mismatch documented at the tutor axis fan-out in TurnSolver.cpp: that
// fan-out ranks every plan against one shared turn-start state rather than the state its own plan
// creates. Correcting the state ALONE measured worse (+18 held-out, MTG_TUTOR_AXIS_POSTLAND), and
// the diagnosis was that it made one input accurate while every other input stayed calibrated to
// the old, wrong one. The point of a plan context is to let the whole cluster move together instead.
//
// Deliberately a raw view, not a copy: it is set on a hot enumeration path and must cost nothing.
// The pointers are owned by the caller and valid only for the duration of the Scope below.
struct PlanContext
{
    const std::vector<Action>* actions   = nullptr;  // the plan's action list
    std::size_t                index     = 0;        // which action is being decided
    const std::string*         land      = nullptr;  // land this plan plays (empty = holds it)
    bool                       land_done = false;    // true if `state` already has that land in play

    // Actions that come AFTER the one being decided -- "the rest of the plan". Defined out-of-line
    // in the provider TU (PlanContextRest, below) so this header stays free of TurnSolver.h.
};

// The rest of the plan as a [begin, end) range. Returns {nullptr, nullptr} when no plan is in scope.
// Declared here and defined in PlanContext.cpp so callers that DO include TurnSolver.h can walk it
// without this header pulling in the Action definition for everyone else.
std::pair<const Action*, const Action*> PlanContextRest(const PlanContext* pc);

// Null when no plan is in scope (real resolution, human play, or a provider call made outside plan
// enumeration). A provider MUST treat null as "no extra information" and behave exactly as before.
const PlanContext* CurrentPlanContext();

// RAII setter. Nests safely: restores whatever was in scope before.
class PlanContextScope
{
public:
    explicit PlanContextScope(const PlanContext* pc);
    ~PlanContextScope();
    PlanContextScope(const PlanContextScope&)            = delete;
    PlanContextScope& operator=(const PlanContextScope&) = delete;

private:
    const PlanContext* m_prev;
};
