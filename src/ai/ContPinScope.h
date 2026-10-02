#pragma once
// Install a breakpoint continuation's during-cast sub-decision pins (ContPins, TurnSolver.h) for the
// duration of its apply -- in the search (ApplyPlanDirect's continuation sites) and in the executor
// (both breakpoint routes), so the two worlds resolve the continuation's casts identically.
// Only SET fields are installed: an unset field leaves any outer pin exactly as it was, so a
// continuation without pins behaves byte-identically to before this scope existed.
#include "TurnSolver.h"
#include "../core/SpellEffects.h"

#include <optional>

struct ContPinScope
{
    std::optional<ScriptedEtbDig>     etbdig;
    std::optional<ScriptedSagaCh1>    saga_ch1;
    std::optional<ScriptedReorder>    ponder;
    std::optional<ScriptedEtbCounter> etbcounter;
    std::optional<ScriptedTutor>      tutor;
    std::optional<ScriptedSacLand>    sac;

    explicit ContPinScope(const ContPins* p)
    {
        if (p == nullptr) { return; }
        if (p->etbdig     >= 0) { etbdig.emplace(p->etbdig); }
        if (p->saga_ch1   >= 0) { saga_ch1.emplace(p->saga_ch1); }
        if (p->ponder     >= 0) { ponder.emplace(p->ponder); }
        if (p->etbcounter >= 0) { etbcounter.emplace(p->etbcounter); }
        if (p->tutor      >= 0) { tutor.emplace(p->tutor); }
        if (!p->sac.empty())    { sac.emplace(p->sac); }
    }
    ContPinScope(const ContPinScope&) = delete;
    ContPinScope& operator=(const ContPinScope&) = delete;
};

// The continuation's own pins, read off its Plan.
inline ContPins ContPinsOf(const TurnSolver::Plan& p)
{
    ContPins c;
    c.etbdig     = p.etbdig_choice;
    c.saga_ch1   = p.saga_ch1_choice;
    c.ponder     = p.ponder_choice;
    c.etbcounter = p.etbcounter_choice;
    c.tutor      = p.tutor_choice;
    c.scry       = p.scry_choice;
    c.sac        = p.sac_pins;
    return c;
}
