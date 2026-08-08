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

// MTG_TUTOR_AXIS_RESOLVE -- DEFAULT ON (adopted 2026-08-05); =0 restores the legacy name-bound
// axis. Bind the searched tutor pick by INDEX resolved at the TRUE per-plan state instead of by
// NAME ranked at the shared pre-land turn-start state (see the full note at TurnSolver's
// TutorAxisResolveMode call sites and Plan::tutor_choice). Shared reader because BOTH the plan
// machinery (TurnSolver) and the provider heuristics (DecisionProviders) branch on it: under
// resolve mode a tutor ranking runs at MID-TURN states (mana spent, source on the battlefield),
// and provider terms that conflate "mana unspent right now" with "mana capacity per turn" --
// calibrated on turn-start states where the two coincide -- must switch to the capacity read
// (see GoblinsProvider turns_to_deploy). Adoption numbers (held-out overnight, per-game
// loss-penalized vs prior GT): antilife d0 -317 (32/0) + searched -3; hinata d0 -11, searched ~0
// net of the gi90/gi158 GT artifacts; goblins d0 +14 / searched +3 (all churn, recovers at 4x
// budget) -- the accepted residual, tracked in docs/design/goblins-tutor-handoff.md section 9.
inline bool TutorAxisResolveEnabled()
{
    static const bool v = EnvOn("MTG_TUTOR_AXIS_RESOLVE", true);
    return v;
}

// MTG_UPKEEP_FLOAT_CLEAR -- DEFAULT ON; =0 restores the legacy carry-over. Empty the mana pool at
// the END OF THE UPKEEP STEP (CR 500.4), i.e. right after the echo pay-or-sacrifice pass, so mana
// over-produced paying an echo cost cannot fund the main phase.
//
// The bug it fixes (viewer issue #6): floating_mana was only cleared at untap and on entering
// combat, and echo is paid off a possibly LUMPY source. Goblins s19 gi18 T4 recorded
// floating_mana {"R": 5} in the pre-combat main -- Three Tree City produced 9 red for a {3}{R}
// echo and the 5 left over stayed spendable. That is not just a display artifact: AvailableManaPool
// adds the reserve, so the SEARCH enumerated lines funded by mana the rules say no longer exists.
//
// Shared reader because the two worlds resolve echo in different files and must clear at the same
// point or diverge: the executor at the top of the pre-combat main (AIEngine::TakeTurn) and the
// rollout at simulated turn-start (TurnSolver::SimulateEndAndStartNextTurn). GT-affecting for decks
// with echo creatures; inert everywhere else (nothing else floats mana during upkeep).
inline bool UpkeepFloatClearEnabled()
{
    static const bool v = EnvOn("MTG_UPKEEP_FLOAT_CLEAR", true);
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
