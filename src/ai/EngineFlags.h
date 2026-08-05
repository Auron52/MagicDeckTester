#pragma once
// Shared readers for env flags that BOTH the executor (AIEngine) and the rollout (TurnSolver)
// must agree on. Each of these was previously a per-TU `static const` copy-pasted into both
// files -- two chances to update one and not the other, which is the executor/rollout lockstep
// failure mode in miniature. One reader per flag; the function-local static means the
// environment is read once per process, same as before.
#include "../core/EnvFlags.h"
#include <cstdlib>
#include <string>

// MTG_LEGACY_STATIC_TAPPED=1: classify land tapped-ness from the STATIC enters_tapped flag in the
// land-priority passes, as before the dynamic fix (byte-identical A/B hatch). See
// AIEngine::TryPlayLand and TurnSolver's greedy_land_name -- the two implement the same passes
// and must stay in lockstep.
inline bool LegacyStaticTapped()
{
    static const bool v = EnvOn("MTG_LEGACY_STATIC_TAPPED");
    return v;
}

// MTG_LAND_CLOSING_WINDOW: drop a still-untapped fastland ahead of an unconditionally-untapped
// land, since only the fastland's window closes. DEFAULT ON; =0 disables (value-aware hatch).
inline bool LandClosingWindowEnabled()
{
    static const bool v = EnvOn("MTG_LAND_CLOSING_WINDOW", true);
    return v;
}

// MTG_TUTOR_AXIS_RESOLVE=1 (default off): bind the searched tutor pick by INDEX resolved at the
// TRUE per-plan state instead of by NAME ranked at the shared pre-land turn-start state (see the
// full note at TurnSolver's TutorAxisResolveMode call sites and Plan::tutor_choice). Shared
// reader because BOTH the plan machinery (TurnSolver) and the provider heuristics
// (DecisionProviders) branch on it: under resolve mode a tutor ranking runs at MID-TURN states
// (mana spent, source on the battlefield), and provider terms that conflate "mana unspent right
// now" with "mana capacity per turn" -- calibrated on turn-start states where the two coincide --
// must switch to the capacity read (see GoblinsProvider turns_to_deploy).
inline bool TutorAxisResolveEnabled()
{
    static const bool v = EnvOn("MTG_TUTOR_AXIS_RESOLVE");
    return v;
}

// MTG_BP_TRACE (diagnosis only): print the breakpoint sequences on both sides -- the EXECUTOR's
// ([bp-exec], AIEngine) and the apply side's ([bp-apply], TurnSolver::ApplyPlanDirect) -- so they
// can be diffed. A searched continuation landing at a different index on the two sides is the
// lockstep defect. See docs/design/post-breakpoint-search.md.
inline bool BpTraceEnabled()
{
    static const bool v = EnvOn("MTG_BP_TRACE");
    return v;
}
