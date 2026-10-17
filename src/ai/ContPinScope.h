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
    std::optional<ScriptedLandSearch> land_search;
    std::optional<ScriptedPutBounce>  put_bounce;   // the continuation's Genesis Wave put Karoo

    explicit ContPinScope(const ContPins* p)
    {
        if (p == nullptr) { return; }
        if (p->etbdig     >= 0) { etbdig.emplace(p->etbdig); }
        if (p->saga_ch1   >= 0) { saga_ch1.emplace(p->saga_ch1); }
        if (p->ponder     >= 0) { ponder.emplace(p->ponder); }
        if (p->etbcounter >= 0) { etbcounter.emplace(p->etbcounter); }
        if (p->tutor      >= 0) { tutor.emplace(p->tutor); }
        if (!p->sac.empty())    { sac.emplace(p->sac); }
        if (!p->land_search.empty()) { land_search.emplace(p->land_search); }
        if (p->put_bounce >= 1)      { put_bounce.emplace(p->put_bounce); }
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
    c.bounce     = p.bounce_choice;
    c.sac        = p.sac_pins;
    c.land_search = p.land_search_pins;
    c.put_bounce  = PutBouncePinOf(p.bounce_choice, p.land_decided, p.land_to_play);
    return c;
}

// A plan's WHOLE-APPLY PAYMENT PINS: the searched hold-vs-tap of the mana creatures (Plan::tapmode_choice)
// and the fresh-mint release (Plan::freshmode_choice). Installed UNCONDITIONALLY -- 0 is the shipped rule,
// and an outer value must not leak in -- and NOT consumed by a reader: every payment of the apply sees the
// same pair, or one plan would be scored under two payment policies. ONE body for three sites: ApplyPlanDirect's
// preamble, the executor's committed plan (AIEngine::TakeTurn), and the executor's SEARCHED breakpoint
// re-solve, whose candidates ApplyPlanDirect scored under their own pair (adopt_resolve_post_apply). A
// rollout breakpoint CONTINUATION is different: it is paid inside the outer plan's apply, under the outer
// plan's pair, so it never installs one (ContPins holds during-cast pins only).
struct PlanPaymentPinScope
{
    ScriptedTapMode   tapmode;
    ScriptedFreshMode freshmode;
    explicit PlanPaymentPinScope(const TurnSolver::Plan& p)
        : tapmode(p.tapmode_choice), freshmode(p.freshmode_choice) {}
    PlanPaymentPinScope(const PlanPaymentPinScope&) = delete;
    PlanPaymentPinScope& operator=(const PlanPaymentPinScope&) = delete;
};
