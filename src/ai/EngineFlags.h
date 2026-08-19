#pragma once
// Shared readers for env flags that BOTH the executor (AIEngine) and the rollout (TurnSolver)
// must agree on. Each of these was previously a per-TU `static const` copy-pasted into both
// files -- two chances to update one and not the other, which is the executor/rollout lockstep
// failure mode in miniature. One reader per flag; the function-local static means the
// environment is read once per process, same as before.
#include "../core/EnvFlags.h"
#include <cstdlib>
#include <string>

// MTG_MAIN2_DROP=1 -- measurement lever (DEFAULT OFF until the adoption A/B is accepted): offer
// the turn's still-unused land drop in the POST-combat main for the autonomous search/executor,
// as the rules allow (CR: a land may be played during either of your main phases). Human play has
// always had this (EnumeratePlansWithLand's s_human_play_drop); the autonomous engine's
// "second main is cast-only" assumption predates main-phase classification, under which a deck
// can draw into a land in main 2 and must be able to play it (measured: hinata gi=99). Read by
// BOTH the search (EnumeratePlansWithLand / FSLineTail / ApplyPlanDirect) and the executor
// (AIEngine fold_land) -- shared reader per the lockstep rule. On adoption this flips to
// default-ON with an MTG_NO_MAIN2_DROP hatch + GT rebaseline.
inline bool Main2DropEnabled()
{
    static const bool v = EnvOn("MTG_MAIN2_DROP");
    return v;
}

// MTG_GARTH_ORDERED=1 -- measurement lever (DEFAULT OFF until the adoption A/B is accepted):
// Garth One-Eye's tap IS the cast of its conjured copy (WotC ruling, already in the card model:
// the copy is cast as the ability resolves -- no holding it). USER doctrine 2026-08-19: "order
// his spells like the rest and he should tap at those times if we choose that option ...
// Because he must cast them immediately." Under the lever the activation joins the ordered
// main-phase cast sequence at the COPY's provider rank (OrderDefOf in ManaPayment.cpp) instead
// of the fixed post-cast dispatch position it has today. Read by the comparator/ladder
// (ManaPayment), the rollout apply (TurnSolver::ApplyPlanDirect) and the executor
// (AIEngine::TakeTurn) -- shared reader per the lockstep rule.
// MEASURED (train, fivecolour, 2026-08-19): searched keys BYTE-IDENTICAL (Garth taps do not
// occur in executed searched games on these seeds -- he lands T4-5 and the games end first);
// d0 1/1000 worse (gi922 7->8): ordered ahead of the casts, the copy's payment competes for
// sources FIRST instead of eating leftovers, and the greedy d0 executor has no whole-turn
// payment planning to compensate (the parked mana-affordability arc's allocation class).
// Kept DEFAULT OFF: faithful to the doctrine, but inert where search decides and net-negative
// at d0. Revisit if a longer-game deck actually exercises Garth's tap.
inline bool GarthOrderedEnabled()
{
    static const bool v = EnvOn("MTG_GARTH_ORDERED");
    return v;
}

// MTG_ACQ_RESOLVE=1 -- measurement lever (DEFAULT OFF until the adoption A/B is accepted):
// mid-phase ACQUISITION re-solve family. A cast that puts new castable resources in hand mid-plan
// without drawing -- a tutor-to-hand fetch (Gamble) or a staged exile dig (Soulfire Eruption's
// damage_equals_top_mv) -- arms the deferred post-cast re-solve exactly like a cantrip draw, so
// the acquired cards are castable in the SAME phase. Historically neither armed anything, which
// was sound only while such casts happened in MAIN 1: the post-combat enumeration picked the
// acquisitions up FOR FREE at the phase boundary. A Main2-classified deck has no later
// enumeration this turn (hinata gi=22 Gamble, gi=6 Soulfire). Read by the rollout apply
// (TurnSolver) and the executor's draw-engine classification (AIEngine) -- shared reader.
inline bool AcqResolveEnabled()
{
    // ADOPTED default-on (USER, 2026-08-19; measured during the Creature Giving order review --
    // the USER's "Is there no breakpoint after casting a tutor?"). Held-out: hinata 12/12 keys
    // green (d5 to -0.087, per-game 551:82 -- the recorded gi=22 Gamble class), creature_giving
    // 12/12 green, goblins green, antilife net-green (two noise-scale d0 keys red). The depth-0
    // executor half (note_draw_engine's tutor clause + the deferred-drop second pass) was
    // completed in the same review. =0 reverts.
    static const bool v = EnvOn("MTG_ACQ_RESOLVE", true);   // DEFAULT ON; =0 disables
    return v;
}

// MTG_ACQ_DIG=1 -- measurement lever (DEFAULT OFF until the adoption A/B is accepted): extend the
// MTG_ACQ_RESOLVE acquisition family to the ETB library dig (etb_dig_count -- Acclaimed Contender,
// the only such card today). The dig puts a same-phase-castable card into hand at resolution, but
// the plan was enumerated before it existed: TurnSolver performs the dig inline and its own comment
// records "the dug card is cast on a later turn, not re-solved this turn". This lever gives the
// DEPTH-0 EXECUTOR a post-cast second pass (AIEngine note_draw_engine) so a dug Knight is castable
// with this turn's leftover mana -- the USER's Knights-review intent ("we probably should encode
// this now and work toward making it part of the calculation", 2026-08-19).
// SCOPE = d0 ONLY, a measured rejection, not an oversight: the first arm also armed the rollout's
// deferred re-solve at searched depths, and held-out it went 6/8 searched keys RED
// (+0.002..+0.006) against d0 4/4 green -- the arming re-biased plan selection toward digger
// lines whose pruned greedy continuation misplayed the committed turn, plus dig-reorder variance.
// Searched depths are byte-identical by construction (no rollout arming, no is_draw_engine
// classification -- nothing to keep in breakpoint lockstep). CAST path only; a VIAL-deployed
// digger has no second pass (the executor's Vial loop has no draw-engine classification) --
// recorded open edge. Shared-reader placement kept for the lockstep comment trail even though
// only the executor reads it today.
inline bool AcqDigEnabled()
{
    // ADOPTED default-on (USER, 2026-08-19) in the d0-only scope above. Held-out (with the
    // Knights order): d0 4/4 keys green (-0.0035..-0.0075), searched byte-identical by
    // construction. The gi154 class is the mechanism: Contender's dig puts a Knight in hand and
    // the second pass casts it with the leftover mana (T5 win -> T4). =0 reverts.
    static const bool v = EnvOn("MTG_ACQ_DIG", true);   // DEFAULT ON; =0 disables
    return v;
}

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

// MTG_VIAL_AXIS -- the Aether Vial upkeep charge is decided IN-SEARCH, as a real plan axis
// (TurnSolver::Plan::vial_charge_choice). DEFAULT ON; `=0` is the exact legacy hatch (the
// out-of-band probe in AIEngine::DecideVialCharge, MTG_SEARCHED_VIAL, decides it as before).
// Adopted 2026-08-18, user-directed: "the option to search needs to remain for all of the decks
// ... even though we won't be taking it in most cases, maybe ever. There are cases where we might
// want to take it, though. If it is really not obvious what decision to make."
//
// WHY THE PROBE IS NOT ENOUGH. It is the shape the 2026-08-06 ruling retired for the cleanup
// discard: a side process that plays nested engine games per candidate and hands the executor a
// pick -- neither the search deciding nor a heuristic pruning. It also cannot see THIS decision at
// all. The Vial deploys a creature whose mana value EQUALS its counter count, so reaching a 3- or
// 5-drop takes several CONSECUTIVE charges; the probe rolls both answers out under a continuation
// that never charges again, so its arms differ by at most one deploy and tie. Measured: 671 probe
// firings across goblins/knights/slivers, 0 deviations on goblins, and disabling it entirely was
// byte-identical over 16,000 held-out goblins games. As an axis the branch re-fans at every level
// of the recursion, so a multi-charge climb is a reachable line -- searched at declared depth under
// the same memo, cutoffs and first-win ladder as everything else, with nested games impossible by
// construction.
//
// NO OBVIOUSNESS GATE. A narrowed variant was built and measured (fan only when a creature sits at
// exactly the current counter AND a bigger one is in hand): 4.6-9.8x less fan-out, and it cut the
// slivers cost from +0.0640 to +0.0140. REJECTED anyway, on two grounds. It is a lossy prune under
// Rule 0b -- "hold at k because I will draw an MV-k creature next turn" becomes unreachable even at
// infinite budget -- and it decides which calls are obvious using the very heuristic the search
// exists to be able to overrule. The branch stays open on every deck.
//
// Read by BOTH the rollout (TurnSolver: variant emission + SimulateBeginningPhase consume) and the
// executor (AIEngine::DecideVialCharge, which retires the probe when the axis owns the decision) --
// shared reader per the lockstep rule.
inline bool VialAxisEnabled()
{
    static const bool v = EnvOn("MTG_VIAL_AXIS", true);
    return v;
}
