#pragma once
// Shared readers for env flags that BOTH the executor (AIEngine) and the rollout (TurnSolver)
// must agree on. Each of these was previously a per-TU `static const` copy-pasted into both
// files -- two chances to update one and not the other, which is the executor/rollout lockstep
// failure mode in miniature. One reader per flag; the function-local static means the
// environment is read once per process, same as before.
#include "../core/EnvFlags.h"
#include "HeuristicArm.h"
#include "../core/GameLogger.h"   // HumanPlayActive / HumanKarooFloatRuleOn (the legacy Karoo replay gate)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// Main2DropEnabled -- ALWAYS TRUE since 2026-09-30 (MTG_MAIN2_DROP deleted; USER HARD RULE,
// docs/design/no-greedy-in-search-window.md). The turn's still-unused land drop is offered in the
// POST-combat main to the autonomous search and executor alike, as the rules allow (CR: a land may
// be played in either main phase). A main 2 that cannot play the land it drew or tutored is a
// capability gap the search cannot branch around -- a heuristic substitute inside the window.
// Kept as a function (not inlined away) so the executor/search lockstep sites still read as one rule.
inline bool Main2DropEnabled() { return true; }

// MTG_UPKEEP_CALL=1 -- measurement lever (DEFAULT OFF until its adoption A/B is accepted): allow a
// Call of the Wild activation in the UPKEEP, before the draw step, on a top the controller stacked
// last turn (docs/design/stompy-top-of-library-consumers.md Item 2, recorded DEFERRED since
// 2026-08-21 with the USER's ruling verbatim). Without it the end-of-turn-tutor -> upkeep-activation
// line cannot be represented at all: by the main phase the draw has already pulled the stacked fatty
// into hand, where a 7-11 MV body must be hard-cast instead of put into play for the activation cost.
//
// Read by BOTH the executor (AIEngine::ResolveUpkeepRevealTop, via GameEngine::UpkeepTail) and the
// rollout (TurnSolver::SimulateEndAndStartNextTurn's upkeep block) -- shared reader per the lockstep
// rule, because a window one world opens and the other does not is a rollout scoring a line the
// executor cannot play. The gate itself (UpkeepRevealTopCandidate) lives in core/SpellEffects.h so
// there is exactly one definition of "is this window open"; this flag only decides whether to ask.
//
// INERT BY CONSTRUCTION elsewhere: Call of the Wild is the ONLY card in cards.json carrying
// activated_reveal_top_cost, and only StompySurprise plays it, so no other deck can reach the block
// even with the lever on. On adoption this flips to default-ON with an MTG_NO_UPKEEP_CALL hatch plus
// a stompy-tier GT rebaseline.
inline bool UpkeepRevealTopEnabled()
{
    static const bool env_on = EnvOn("MTG_UPKEEP_CALL");
    return env_on;
}

// MTG_LETHAL_CUT_VERIFY (DEFAULT ON; =0 -> the unverified cut, byte-identical to before) -- the
// board-lethal short-circuit (DecisionProvider::UseLethalShortCircuit; Solve + EnumeratePlans) fires
// only when the attack-only plan REALLY kills: applied on a copy and its combat simulated
// (SimulateCombat, the executor's lockstep combat). Otherwise the frame is restored and enumerates
// exactly as with no cut. The cut's guard reads PendingAttackDamage, which is a PROJECTION: its
// Arcanum Wings combat-swap term (CountAttackGatherPump -> ApplyCombatAuraSwap on a copy of the
// main-phase board) pays the {2}{U} with mana still FLOATING in main 1, which empties before combat
// (CR 500.4). Bruna overnight s4004 gi334 (seed 4338) T8: Remote Farm paid Arcanum Wings and floated
// a {W}; at the executor's breakpoint after the Wings cast the projection read 20 against 20 life (0
// with the pool drained), the cut collapsed the continuation list to attack-only, the committed
// line's searched continuation (Avacyn's Pilgrim + Greaves onto it, whose {W} pays the swap) was no
// longer in the list, and combat found the swap unpaid -- T8 win -> unwon at d3 b10 and d8 b0 alike.
// Read in TurnSolver only (both cut sites); kept here as the one reader per the flag convention.
inline bool LethalCutVerifyEnabled()
{
    static const bool on = EnvOn("MTG_LETHAL_CUT_VERIFY", true);   // DEFAULT ON; =0 disables
    return on;
}

// MTG_SAC_OUTLET_PAY_SCOPE (Vampires A2 0.4, 2026-10-18; DEFAULT ON, =0 restores the mis-modelled
// payment) -- a creature-sac outlet's mana cost is an ACTIVATED ABILITY OF A CREATURE SOURCE, so both
// trailing passes pay it under CreatureAbilityPayScope, exactly as ActivatePump / a creature-source
// ActivatePermAbility already do (D12). Before this the scope was missing at both sac-outlet payments,
// so Secluded Courtyard's coloured mana ("an ability of a creature source of the chosen type") could
// not pay Falkenrath Pit Fighter's {R} -- or Siege-Gang's / Pashalik's {R} in any list that pairs them
// with a Courtyard. A RULES fix, not a heuristic; the =0 hatch exists to attribute a moved game.
// Read by BOTH trailing passes (TurnSolver's ApplyPlanDirect and AIEngine's executor twin).
inline bool SacOutletPayScopeOn()
{
    static const bool v = EnvOn("MTG_SAC_OUTLET_PAY_SCOPE", true);
    return v;
}

// MTG_VAMP_GY_LANDFALL_HELD_LAND (Vampires A2 0.5, 2026-10-18; DEFAULT ON, =0 = the USER rule
// unexempted) -- the NARROW exemption from the main-2 held-land bar (USER 2026-09-30: "Only newly
// arrived lands in second main would be okay") for a GRAVEYARD-LANDFALL deck. THE BAR'S PREMISE
// FAILS HERE: "main 2: Falkenrath Pit Fighter sacrifices Bloodghast (legal only after combat damage),
// THEN play the held land -> Bloodghast returns" ends the turn with Bloodghast on the battlefield,
// while every main-1-land line ends with it in the graveyard -- main 1 cannot express it, so under the
// bar the line is unfindable at ANY budget. The exemption applies ONLY when a graveyard-landfall card
// (CardDefinition::gy_landfall_watcher) is in our graveyard at the main-2 state, or can be put there
// by a main-2 activation (one is on the battlefield / in hand with a live discard or sacrifice outlet).
// Everything else keeps the USER rule. USER QUESTION OPEN (ledger open question 1): this is the
// default the integrator took; the user decides whether it stays. Read by the enumerator only (the
// land options a main-2 plan may carry), and the executor plays whatever plan was chosen.
inline bool VampGyLandfallHeldLandOn()
{
    static const bool v = EnvOn("MTG_VAMP_GY_LANDFALL_HELD_LAND", true);
    return v;
}

// MTG_FUNGUS_SPORE_POOL (ADOPTED 2026-09-22, DEFAULT ON; =0 reverts, heurarm slot so one pooled
// batch carries both arms) -- treat every interchangeable spore outlet as ONE POOL, spent
// oldest-first, so the only enumerated axis is HOW MANY Saprolings to make and never WHICH body pays.
//
// ---- WHY IT WAS ADOPTED, AND WHAT IT IS WORTH -------------------------------------------------
// It shipped default-OFF on 2026-09-18 and sat there, which made it dead code by the repo's own
// rule. What moved it was the MULLIGAN GEN: Fungus keep-generation is dominated by a tail of
// degenerate cells, and 348 rollouts (0.056% of all of them) were consuming 31.4% of the machine,
// with a single keep-rollout reaching 22.1 MINUTES against a 3 ms budget. Those rollouts are all
// Doubling Season / Utopia Mycon / Thallid hands -- i.e. exactly the boards where "which body pays"
// multiplies out.
//
// ROOT CAUSE, for anyone tempted to look at the budget instead: the greedy rollout leaf's subset
// enumeration is NOT budgeted. One degenerate game scored 12,578,432 subsets where a typical one
// scores 3,120 (4,031x) while the search's own units_total saw only 458,797 -- the budget cannot
// see that work, so it cannot bound it. Pooling collapses the powerset at the EMISSION site, which
// is why it reaches a cost the budget never could: 12.6M -> 3.3M subsets on that same game.
//
// MEASURED (HELD OUT, 3,200 fresh games over 4 seeds, both settings):
//   gen settings  d1/b3 : avg win turn IDENTICAL on every seed, wall -40.1% (1.67x)
//   play settings d3/b10: avg win turn IDENTICAL on every seed, wall  -8.4% (1.09x)
//   tail (400 games d1/b3): games over 2 s 15 -> 5, worst game 12,966 ms -> 6,296 ms
// The play SHAPE changes (digest moves) -- it picks a different body to pay -- but on 3,200 games
// the outcome never did. That is the shape the user's ruling predicts, and it is the evidence for
// adopting a HEURISTIC rather than a proof.
//
// USER 2026-09-18: *"we should de-duplicate the choice of which source a saproling comes from, the
// oldest entry that has 3 counters first"*, and the licence that makes it admissible:
// *"Heuristically, we know that the source of the saproling does not matter in this deck, so we can
// safely choose any of them that has enough counters to be the first one."* It is a HEURISTIC, not
// a proof -- spending Thallid A's counters rather than Thallid B's leaves a different DISTRIBUTION
// of residual counters (the total is identical: every pop costs exactly the same three) and a
// future turn could in principle care which body kept them. The user has ruled that it does not for
// this deck, which is exactly the deck-provider scope the repo reserves for narrowing.
//
// WHY THE GENERAL FOLD COULD NOT DO THIS (measured, fungus-token-search-cost.md): the shipped
// canonical-prefix fold is lossless and was tried here first as MTG_FOLD_COUNTER_SOURCES. It moved
// `units_total` by ZERO, for two structural reasons this lever sidesteps rather than fixes --
// FinalizeFoldTags CONDITION 2 drops any class whose source emitted more than one action (a Thallid
// on six counters emits k=1 AND k=2, so every class died: drop_src=116,427), and the prefix guard
// is gated `from_odometer`, which the searched enumeration never goes through. Pooling at the
// EMISSION site needs neither: it emits one action per COUNT instead of one per (source,count), so
// the powerset collapses before any guard is consulted.
//
// Read by BOTH the rollout (TurnSolver's spore emission and its ApplyPlanDirect twin) and the
// executor (AIEngine's spore apply) -- shared reader per the lockstep rule, because an emission
// that pooled and an apply that did not would spend the wrong bodies.
inline bool SporeSourcePoolEnabled()
{
    static const bool env_on = EnvOn("MTG_FUNGUS_SPORE_POOL", true);
    return heurarm::Flag(heurarm::FUNGUS_SPORE_POOL, env_on);
}

// MTG_FOLD_XNAME -- DEFAULT OFF; =1 enables. Key a card's FOLD IDENTITY on its BEHAVIOUR
// (CardDefinition::behaviour_identity) instead of on its NAME. Full rationale, the card-data
// verification and the digest's failure direction are in the heurarm::FOLD_XNAME slot and at
// HandCardContentHash (TurnSolver.cpp).
//
// SHARED READER PER THE LOCKSTEP RULE. Two TUs decide the same question about the same card:
// TurnSolver's HandCardContentHash (the hand-cast fold, the permanent half, and the act-source
// fold all reach it) and DecisionProviders' PurchaseKey (the equip route's DFS dedup). A
// hand-cast fold that merged two names while the route's purchase key still split them would
// have the two halves disagree about what ONE CARD is -- the executor/rollout lockstep failure
// mode in miniature, and the exact shape convention 4 exists to prevent.
// DEFAULT ON since 2026-10-11; `=0` disables. Adopted as an IDENTITY FOLD on the strongest evidence
// shape available: over 5,000 paired rollouts it moved ZERO win turns (changed=0) at 0.986x, and
// Gate 1 (MTG_FOLD_VERIFY + MTG_BF_CENSUS, the recoverability bar) PASSED non-vacuously with
// 569,286 recoverable rejections and UNRECOVERABLE=0 -- 24,108 MORE rejections than the control,
// every one with a legal enumerable twin. Gate 2 PASSED 6 of 6 cells at b768.
// Suite scope was MEASURED rather than predicted: 106 of 114 cases byte-identical, and only ONE
// non-Equipment deck moves (stompy d3, aggregate unchanged) -- because the fold needs two
// behaviourally-identical cards LIVE AT ONCE, not merely present in the decklist. Static analysis
// had put the ceiling at 15 cases.
inline bool FoldXnameOn()
{
    static const bool env_on = EnvOn("MTG_FOLD_XNAME", true);             // DEFAULT ON
    return heurarm::Flag(heurarm::FOLD_XNAME, env_on);
}

// MTG_DRAGONSTORM_IRENCRAG_CLOSER (DEFAULT ON; =0 disables) -- USER order rule 7: Irencrag Feat is cast
// second-to-last, and after it only the CLOSER (Dragonstorm or a hard-cast Dragon -- the test
// DragonstormProvider::CastUnderCastCap applies to the offers after it). A restrictor that nothing can
// follow floats seven red no spell spends (the float empties before combat, so not even a pump). Two
// sites, one reader: the provider's subset test (OrderedSubsetRealisable -- a subset with the restrictor,
// no closer and no Apex whose exile could bring one) and the shared post-Apex merge
// (MergeBreakpointLeftovers -- a LEFTOVER restrictor with no closer after it and no Apex before it is not
// cast; it stays in hand). Executor and rollout both go through the merge -> lockstep.
inline bool DragonstormIrencragCloserOn()
{
    static const bool env_on = EnvOn("MTG_DRAGONSTORM_IRENCRAG_CLOSER", true);
    return heurarm::Flag(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, env_on);
}

// MTG_DRAGONSTORM_D0_SPLICE_REFUSE (DEFAULT ON; =0 disables) -- the DEPTH-0 RUNNER (TurnSolver::InD0Runner,
// the permitted greedy site) does not commit a Dragonstorm subset holding a SPLICED cast that no legal order
// pays in full: DragonstormProvider::OrderedSubsetRealisable refuses it wherever the exact walk fails, not
// only at the spliced cast. Read at that one site (both subset evaluators call it); the search window --
// rollouts and searched frames -- is untouched. docs/design/dragonstorm-d0-splice-refusal.md.
inline bool DragonstormD0SpliceRefuseOn()
{
    static const bool env_on = EnvOn("MTG_DRAGONSTORM_D0_SPLICE_REFUSE", true);
    return heurarm::Flag(heurarm::DRAGONSTORM_D0_SPLICE_REFUSE, env_on);
}

// MTG_FB_HOLD_BANKED (DEFAULT ON; =0 disables) -- the combat pump holds BANKED sources (depletion and
// storage lands) back unless the full pump is lethal. Read by both pump sites (AIEngine::Firebreathe and
// the rollout's SimulateCombat) through FbHoldBankedThisCombat (Combat.cpp) -> lockstep.
inline bool FbHoldBankedOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_BANKED", true);
    return heurarm::Flag(heurarm::FB_HOLD_BANKED, env_on);
}

// MTG_FB_HOLD_BANKED_NEXT_TURN (DEFAULT ON; =0 -> the this-combat lethal test alone) -- the banked hold
// is dropped when spending the banked sources now buys a NEXT-turn kill that holding them misses.
// Read only inside FbHoldBankedThisCombat (Combat.cpp), so executor and rollout stay in lockstep.
inline bool FbHoldBankedNextTurnOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_BANKED_NEXT_TURN", true);
    return heurarm::Flag(heurarm::FB_HOLD_BANKED_NEXT_TURN, env_on);
}

// MTG_FB_HOLD_SEARCHED (DEFAULT ON; =0 -> the verdict taken at every node, byte-identical) -- the banked
// hold is SEARCHED: the verdict of FbHoldBankedThisCombat only ORDERS "hold the bank" vs "spend it", the
// search branches over both (Plan::fb_hold_choice, fanned in AppendSubdecisionAxes) and the pump honours
// the pinned side (GameState::scripted_fb_hold). Read by the axis fan-out (TurnSolver.cpp) and the one
// helper both pump sites call (Combat.cpp), so executor and rollout stay in lockstep.
inline bool FbHoldSearchedOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_SEARCHED", true);
    return heurarm::Flag(heurarm::FB_HOLD_SEARCHED, env_on);
}

// MTG_FB_HOLD_FLIP_DOMINANCE (DEFAULT ON; =0 -> every contested flip is searched) -- the searched banked
// hold's other side is read as the verdict where it cannot be strictly better: no post-combat main this
// turn and either world projected lethal next turn (FbFlipDominatedByNextTurn, Combat.cpp). A sound
// collapse: the other side can at best tie a next-turn kill, and ties go to the base plan. Read only
// inside FbHoldBankedThisCombat (both pump sites), so executor and rollout stay in lockstep.
inline bool FbHoldFlipDominanceOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_FLIP_DOMINANCE", true);
    return heurarm::Flag(heurarm::FB_HOLD_FLIP_DOMINANCE, env_on);
}

// MTG_FB_HOLD_FOLD_FREE (DEFAULT ON; =0 -> charged before the apply, as every candidate) -- a searched
// banked-hold variant's node unit (FSLineWin kFsPre, the la_cand loop's kLookaheadCand) is charged only
// once MTG_COMBAT_PIN_FOLD has NOT folded it: a variant whose flip turned out uncontested or dominated is
// its base plan's combat, so it re-prices to nothing. Re-prices only: never read at an unlimited budget's
// outcome (the fold and the scoring are unchanged), so nothing becomes unfindable.
inline bool FbHoldFoldFreeOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_FOLD_FREE", true);
    return heurarm::Flag(heurarm::FB_HOLD_FOLD_FREE, env_on);
}

// MTG_FB_HOLD_STRICT (DEFAULT ON; =0 -> a variant ties like any candidate) -- the verdict ORDERS the two
// sides of a searched banked hold, so the other side displaces the incumbent only by a STRICTLY earlier
// win (SolveWithLookahead's candidate loop; FSLineWin's frontier is strict already): it never takes an
// equal-win-turn tie on the graded leaf, and its rollout is asked only about earlier turns (the exact tie
// prune's cutoff), skipped when none is left. Complete at any budget: a line the other side wins earlier
// is still found; an equal-turn line is the same result.
inline bool FbHoldStrictOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_STRICT", true);
    return heurarm::Flag(heurarm::FB_HOLD_STRICT, env_on);
}

// MTG_PAYOFF_REDUCER_SINK_NEEDED (DEFAULT ON; =0 -> every reducer subset admitted, byte-identical) -- the
// Dragonstorm payoff prune's reducer sink (MTG_PAYOFF_PRUNE_REDUCER_SINK) admits a ritual-bearing subset
// whose only payoff is a spell-cost reducer (Ruby Medallion) ONLY when the subset's non-ritual casts are
// NOT already payable from the free sources (untapped non-banked lands + the floating pool) -- i.e. when a
// ritual really funds the reducer. Otherwise every ritual in it is burnt, and the ritual-free sibling
// subset (the same casts, the rituals still in hand) dominates it. Read by EnumeratePlans' branch-list
// prune only (TurnSolver.cpp).
inline bool PayoffReducerSinkNeededOn()
{
    static const bool env_on = EnvOn("MTG_PAYOFF_REDUCER_SINK_NEEDED", true);
    return heurarm::Flag(heurarm::PAYOFF_REDUCER_SINK_NEEDED, env_on);
}

// MTG_LINE_FLOAT_TAP_FIRST (DEFAULT ON; =0 disables) -- the shared payer's line-aware float rung
// (TapForCostSharedImpl, ManaPayment.cpp). One payer for executor, rollout and the payability walks.
inline bool LineFloatTapFirstOn()
{
    static const bool env_on = EnvOn("MTG_LINE_FLOAT_TAP_FIRST", true);
    return heurarm::Flag(heurarm::LINE_FLOAT_TAP_FIRST, env_on);
}

// MTG_LINE_FLOAT_UNSEEN_GUARD (DEFAULT ON; =0 disables -> byte-identical to the rung before it) -- the
// LINE_FLOAT_TAP_FIRST rung's creature-only-colour guard also covers demand the line cannot NAME yet:
// while the applying line casts a breakpoint opener (g_line_opens_unseen -- Apex of Power's impulse
// exile, a cantrip, a tutor to hand), an Unclaimed Territory / Cavern of Souls is not "spare" for a
// noncreature payment unless every colour it makes for a creature is also on offer from the reserve or
// another unrestricted untapped source. dragonstorm s4050 g46 / s5254 g249 T4: Lotus Bloom's {R}{R}{R}
// floated, the rung paid the first Seething Song's generic with the Territory, and Apex then exiled
// Dragonlord Kolaghan, whose {B} only the Territory made -- the T4 kill was gone at every depth and
// budget (d8 b0 too). EXCEPTION -- the guard yields to a shortage the line NAMES: if the plain payment
// would leave a colour a later pip of the line owes with no provider at all (none floating, no untapped
// source making it for a noncreature spell; a pay-sac source counts as every colour), only the
// known-demand guard stands (dragonstorm s1112019 gi19: holding the Territory left Desperate Ritual's
// {R} unpaid). Board state only -- the library is never read. Inert unless LineFloatTapFirstOn() (it
// only narrows that rung's spare set).
inline bool LineFloatUnseenGuardOn()
{
    static const bool env_on = EnvOn("MTG_LINE_FLOAT_UNSEEN_GUARD", true);
    return LineFloatTapFirstOn() && heurarm::Flag(heurarm::LINE_FLOAT_UNSEEN_GUARD, env_on);
}

// MTG_LINE_SPARE_GENERIC_FIRST (DEFAULT ON; =0 disables) -- the shared payer's line-aware generic tap
// (produce_impl in TapForCostSharedOnce, ManaPayment.cpp): a colour-short line's generic pip taps a SPARE
// source first. One payer for executor, rollout and the payability walks -> lockstep by construction.
inline bool LineSpareGenericFirstOn()
{
    static const bool env_on = EnvOn("MTG_LINE_SPARE_GENERIC_FIRST", true);
    return heurarm::Flag(heurarm::LINE_SPARE_GENERIC_FIRST, env_on);
}

// MTG_LINE_SPARE_TIGHT (DEFAULT ON since 2026-10-15; =0 restores the strict test) -- LineSpareSourceMask's
// SHORT test also fires at ZERO surplus: the rest of the line owes EXACTLY what the float plus the non-spare
// board can make of a colour, so a generic pip that spends one of those units strands a later pip. Same
// payer, same lockstep. Measured in docs/design/angels-cast-order-2026-10-14.md: it clears 10 of the 13
// Angels lost lines; outside Angels 0 slower games (dragonstorm d3 1 faster, a few digest-only games).
inline bool LineSpareTightOn()
{
    static const bool env_on = EnvOn("MTG_LINE_SPARE_TIGHT", true);
    return heurarm::Flag(heurarm::LINE_SPARE_TIGHT, env_on);
}

// MTG_PAY_PAIN_LAST (Vampires sweep F, 2026-10-18; DEFAULT ON, =0 restores the historical order) --
// COLOURED MANA THAT COSTS LIFE IS THE PAYER'S LAST RESORT. The scarcity greedy (TapForCostSharedOnce,
// ManaPayment.cpp) ranks a source whose tap for the pip being paid is a damaging COLOURED mode behind
// every painless plain land, and the backtracker (TapForCostBacktrackTop, SpellEffects.cpp -- the
// per-cast fallback AND the whole-turn prepay's joint solve) first solves with those modes withheld,
// re-solving unrestricted only when no painless assignment exists; the prepay's shrink post-pass
// (BatchPrepayMainCasts) refuses a give-back whose re-solve costs life. A painland's painless {C} and Ancient
// Tomb's colourless burst are not coloured modes and are untouched. Unarmed boards only: Prevent Damage's
// armed policy (dmgev::PainAwarePay / PayWithPain) owns pain there, deliberate pain included. One payer
// for executor and rollout, so lockstep holds by construction.
// MTG_PAY_DEPLETION_WASTE / MTG_PAY_DEPLETION_WASTE_ANY (2026-10-22; DEFAULT OFF pending the A/B) -- a
// depletion land's tap (Saprazzan Skerry {U}{U}, Sandstone Needle {R}{R}, Remote Farm, Peat Bog, ...) that
// would make more mana than this payment and the rest of the line can use is ranked behind every painless
// plain source (the pain band): its counter is a finite resource and the LAST one kills the land. _ANY
// covers every counter, the base flag only the last. Ordering, never a filter. See the key block in
// ManaPayment.cpp's scarcity loop.
inline bool PayDepletionWasteAnyOn()
{
    static const bool env_on = EnvOn("MTG_PAY_DEPLETION_WASTE_ANY");
    return heurarm::Flag(heurarm::PAY_DEPLETION_WASTE_ANY, env_on);
}
inline bool PayDepletionWasteOn()
{
    static const bool env_on = EnvOn("MTG_PAY_DEPLETION_WASTE");
    return PayDepletionWasteAnyOn() || heurarm::Flag(heurarm::PAY_DEPLETION_WASTE, env_on);
}
// MTG_PAY_DEPLETION_HOLD (2026-10-22; DEFAULT OFF -- the cross-deck A/B and the adoption are the USER's) --
// the USER's rule for depletion lands: *"The smartest rule for depletion lands would hold the depletion land
// if it can do so and use it otherwise. At least, it should hold it over non-creature source that are not
// consumed."* One shared payer, so executor and rollout move together:
//   * the per-cast greedy (TapForCostSharedOnce) finds, once per payment, the largest set of depletion lands
//     the REST of the payment plus the REST OF THE LINE (g_line_unpaid_cost) can still be paid without, from
//     the sources it ranks ahead of them -- colour-exact (Hall) -- and ranks exactly those behind every
//     plain-tier source (DepletionHoldMask). A depletion land the line needs keeps its old rank, so it pays
//     first instead of a source the line could otherwise have kept.
//   * the whole-turn prepay (TurnSolver::BatchPrepayMainCasts) gets the same set as a PARTIAL depletion rung
//     between "hold every depletion land" and "release them all".
// An ORDER, never a filter: a held land still pays whenever nothing else can.
inline bool PayDepletionHoldOn()
{
    static const bool env_on = EnvOn("MTG_PAY_DEPLETION_HOLD");
    return heurarm::Flag(heurarm::PAY_DEPLETION_HOLD, env_on);
}

// MTG_NIV_LOOKOUT_NIV_ONLY (DEFAULT OFF): Tandem Lookout's pairing default waits for a Niv
// (NivMizzetComboProvider::SoulbondPartner); the soulbond twin enumerator (AppendSoulbondDeclineVariants)
// reads the same flag to offer "pair anyway" wherever that default waits.
inline bool NivLookoutNivOnlyOn()
{
    static const bool env_on = EnvOn("MTG_NIV_LOOKOUT_NIV_ONLY");
    return heurarm::Flag(heurarm::NIV_LOOKOUT_NIV_ONLY, env_on);
}

inline bool PayPainLastOn()
{
    static const bool env_on = EnvOn("MTG_PAY_PAIN_LAST", true);
    return heurarm::Flag(heurarm::PAY_PAIN_LAST, env_on);
}

// MTG_PAY_ENERGY_LAST (2026-10-21; DEFAULT ON, =0 -> energy counts as painless) -- the amendment pain-last
// needed once EldraziDisplacerFlicker met it: a coloured tap that spends ENERGY (Aether Hub,
// energy_per_colored_tap) is a costly mode exactly like a damaging one. Life is nearly free against the
// passive opponent while a Hub's energy is one coloured mana for the whole game, so "painless first"
// spent it to save a life point: smoke edf d0 s1001 gi712 paid Cloud of Faeries' {U} with the Hub instead
// of Yavimaya Coast, had no {W}{W} for Emiel on T3, and lost a T4 win. Every site that bands, withholds or
// guards a damaging coloured mode for pain-last does the same for an energy-paid one (ManaPayment.cpp's
// greedy key and filter feed, SpellEffects.cpp's painless-first pass and its trigger, the prepay
// give-back guard); between the two, the historical ladder decides, as it did before pain-last. Only a
// card with energy_per_colored_tap is touched (Aether Hub; EDF is the only deck running it).
inline bool PayEnergyLastOn()
{
    static const bool env_on = EnvOn("MTG_PAY_ENERGY_LAST", true);
    return PayPainLastOn() && heurarm::Flag(heurarm::PAY_ENERGY_LAST, env_on);
}

// MTG_PAINLESS_AURA_PIP (2026-10-27; DEFAULT ON, =0 -> the historical coloured tap): a coloured pip off a
// painland whose land Aura already pays it takes the painless {C} face -- see PainlessAuraPipColor
// (ManaPayment.cpp) for the rule and the EDF numbers. One reader for the shared payer.
inline bool PainlessAuraPipOn()
{
    static const bool env_on = EnvOn("MTG_PAINLESS_AURA_PIP", true);
    return heurarm::Flag(heurarm::PAINLESS_AURA_PIP, env_on);
}

// MTG_PAY_SHRINK (2026-10-27; DEFAULT ON, =0 -> the backtracker's first covering tap set) -- the per-cast
// payer's give-back: when its backtracker fallback tapped more sources than the bill has mana points,
// re-solve with each one held back (most flexible first) and keep it untapped if the bill still pays from
// the rest of that payment's taps (ManaPayment.cpp, TapForCostSharedOnceImpl).
inline bool PayShrinkOn()
{
    static const bool env_on = EnvOn("MTG_PAY_SHRINK", true);
    return heurarm::Flag(heurarm::PAY_SHRINK, env_on);
}

// MTG_PREPAY_COLOURS (2026-10-27; DEFAULT ON, =0 -> the flat joint bill and the any-colour decline): the
// whole-turn prepay keeps the casts' hybrid pips in its joint bill and pins the any-colour units of its
// pool to the colours the bill needs -- see PrepayColoursFit (TurnSolver.cpp).
inline bool PrepayColoursOn()
{
    static const bool env_on = EnvOn("MTG_PREPAY_COLOURS", true);
    return heurarm::Flag(heurarm::PREPAY_COLOURS, env_on);
}

// MTG_PREPAY_AURA_HOST (2026-10-27; DEFAULT ON, =0 -> the host is an ordinary source of the joint solve):
// the whole-turn prepay holds the untapped host of every land Aura the batch casts out of its solve --
// see PrepayAuraHostHold (TurnSolver.cpp). The per-cast payer's twin is AIEngine's RESERVE THE AURA'S
// OWN HOST.
inline bool PrepayAuraHostOn()
{
    static const bool env_on = EnvOn("MTG_PREPAY_AURA_HOST", true);
    return heurarm::Flag(heurarm::PREPAY_AURA_HOST, env_on);
}

// MTG_PREPAY_AURA_STAGE (2026-10-27; DEFAULT ON, =0 -> the hold's decline): a prepay the held land-Aura hosts
// leave unpayable is tried STAGED (an apply pre-loads the held Auras' bills, paid without the hosts, when the
// rest is payable with host + bonus; the rest then pays per cast) before it declines -- see THE LAND AURA
// STAGED SOLVE (TurnSolver.cpp).
inline bool PrepayAuraStageOn()
{
    static const bool env_on = EnvOn("MTG_PREPAY_AURA_STAGE", true);
    return heurarm::Flag(heurarm::PREPAY_AURA_STAGE, env_on);
}

// MTG_PREPAY_AURA_UNHELD (2026-10-27; with MTG_PREPAY_AURA_STAGE): a batch neither the hold nor the staged
// split pays is retried with the hosts as ordinary sources before it declines. DEFAULT OFF (USER decision owed):
// it pays batches the per-cast walk strands (EDF census: APPLY-DROP 304 -> 225 over 142 games), but the host it
// taps loses its Aura's bonus, and EDF held-out seed 1010001 gi2852 is T3 -> T4 even at d8 b0 (the search's
// rollouts pay through the same retry, so the Conservatory line it would win with reads worse on turn 1).
inline bool PrepayAuraUnheldOn()
{
    static const bool env_on = EnvOn("MTG_PREPAY_AURA_UNHELD");
    return heurarm::Flag(heurarm::PREPAY_AURA_UNHELD, env_on);
}

// MTG_SCARCE_COLOR_MASK (DEFAULT OFF; heurarm SCARCE_COLOR_MASK since 2026-10-28): the per-cast payer's held first
// attempt keeps the line's scarce colour providers up -- see ScarceColorHoldMask (ManaPayment.cpp). Held-out A/B (seed
// 1080001, 39 decks, 20,910 paired games): 3 slower / 0 faster (EDF gi103, Hinata gi223, Snow gi239), work +0.18%
// (EDF +1.29%) -- not adopted; the losses are owed a root cause.
inline bool ScarceColorMaskOn()
{
    static const bool env_on = EnvOn("MTG_SCARCE_COLOR_MASK");
    return heurarm::Flag(heurarm::SCARCE_COLOR_MASK, env_on);
}

// MTG_AUTO_LINE_GENERIC (2026-10-28; DEFAULT ON, =0 -> WUBRG outside human play): the three line-aware generic-pip
// orders (SpendFloatingTowardCost's surplus-first drain, ConsumeFloatingAny, LineDemandAnyPipColor -- SpellEffects.h)
// outside human play too, for lines that name everything they will cast. Read through LineGenericOrderLive
// (SpellEffects.h), which adds human play. Held-out A/B (seed 1080001, 39 decks at play settings, 20,910 paired
// games): 0 slower / 1 faster (Dragons), work +0.024%.
inline bool AutoLineGenericOn()
{
    static const bool env_on = EnvOn("MTG_AUTO_LINE_GENERIC", true);
    return heurarm::Flag(heurarm::AUTO_LINE_GENERIC, env_on);
}

// MTG_PAY_REFINE (2026-10-28; DEFAULT OFF pending the USER, =1 -> on): after an outermost payment that cost life,
// energy or a haste-tagged {C} on a noncreature bill, or floated a surplus off two or more taps, the payer asks the
// mana oracle for the cheapest payment of the same bill out of the same taps that dominates it (ManaOracle.h
// RefinePayment) and makes that one instead. Shared payer: search, executor and human auto-tap alike.
// OFF because one held-out slower game does not recover at d8 b0 (NivMizzetCombo seed 1180001 gi521: the refined
// Ponder payment leaves the greedy payer a different next choice) -- the adoption call is the USER's
// (docs/design/mana-completeness-oracle.md section 13).
inline bool PayRefineOn()
{
    static const bool env_on = EnvOn("MTG_PAY_REFINE", false);
    return heurarm::Flag(heurarm::PAY_REFINE, env_on);
}

// MTG_REAL_PAY_SCRIPT (2026-10-28; DEFAULT OFF pending its A/B, =1 -> on): real play pays the chosen plan with the
// mana oracle's realisation when the engine's is dominated at the line level (ManaOracle.h PlanPaymentScript /
// RealPayScope; AIEngine::TakeTurn). Its first form also paid DROPS and was 17 slower / 0 faster (seed 1130001).
inline bool RealPayScriptOn()
{
    static const bool env_on = EnvOn("MTG_REAL_PAY_SCRIPT", false);
    return heurarm::Flag(heurarm::REAL_PAY_SCRIPT, env_on);
}

// MTG_PAY_REFINE_LAZY / MTG_PAY_REFINE_MEMO (2026-10-28; DEFAULT ON): the refine's cost, not its choice -- the
// supersets that cannot win are not verified, and a repeated refine is read back (ManaOracle.cpp RefinePayment).
inline bool PayRefineLazyOn()
{
    static const bool env_on = EnvOn("MTG_PAY_REFINE_LAZY", true);
    return heurarm::Flag(heurarm::PAY_REFINE_LAZY, env_on);
}
inline bool PayRefineMemoOn()
{
    static const bool env_on = EnvOn("MTG_PAY_REFINE_MEMO", true);
    return heurarm::Flag(heurarm::PAY_REFINE_MEMO, env_on);
}
inline bool PayRefineWildOutOn()
{
    static const bool env_on = EnvOn("MTG_PAY_REFINE_WILD_OUT", true);
    return heurarm::Flag(heurarm::PAY_REFINE_WILD_OUT, env_on);
}
inline bool OracleFaceFoldOn()
{
    static const bool env_on = EnvOn("MTG_ORACLE_FACE_FOLD", true);
    return heurarm::Flag(heurarm::ORACLE_FACE_FOLD, env_on);
}

// MTG_PAY_NARROW_RESTRICTED_FIRST (Vampires sweep L, 2026-10-18; DEFAULT ON, =0 -> battlefield order) --
// among colored_creature_only sources, spend one WITHOUT colored_creature_ability_ok (Unclaimed Territory,
// Cavern of Souls, Sliver Hive, Voldaren Estate) before one WITH it (Secluded Courtyard): the Courtyard's
// colour is the only restricted mana that can also pay a creature source's ABILITY, so it is the one to
// keep. A within-rank tiebreak in the greedy, and the first equally-flexible source the whole-turn
// prepay's shrink post-pass (BatchPrepayMainCasts) tries to give back. No backtracker candidate order:
// its first-found assignment taps the leading sources whenever a solution exists below them (the
// generic-representative colour is explored first), so an order among two cco lands it taps either way
// decides nothing -- the shrink is what returns a surplus source. Same shared payer, same lockstep.
inline bool PayNarrowRestrictedFirstOn()
{
    static const bool env_on = EnvOn("MTG_PAY_NARROW_RESTRICTED_FIRST", true);
    return heurarm::Flag(heurarm::PAY_NARROW_RESTRICTED_FIRST, env_on);
}

// MTG_DRAGONSTORM_DRAGON_ORDER (DEFAULT ON; =0 disables) -- DragonstormProvider::CastOrderingCandidates
// also offers every distinct order of the set's hard-cast Dragons (USER order on). Read only there; the
// searched order rides Plan::searched_order into both apply worlds.
inline bool DragonstormDragonOrderOn()
{
    static const bool env_on = EnvOn("MTG_DRAGONSTORM_DRAGON_ORDER", true);
    return heurarm::Flag(heurarm::DRAGONSTORM_DRAGON_ORDER, env_on);
}

// MTG_DIG_SEARCHED_CHOICE (DEFAULT ON; =0 restores the automatic dig, byte-identical) -- the cycle /
// sac-to-draw dig is a SEARCHED dig / no-dig choice wherever the provider's gate lets it fire, never an
// automatic action inside the search window (USER 2026-10-13: "'Main 1's automatic cycle' ... It
// shouldn't be automatic"). Read by the enumerator (AppendSubdecisionAxes: the no-dig twin and the
// pre-gate), the post-apply dedup (PlanIsAxisVariant) and the executor's reactive dig
// (AIEngine::UseSurplusLandAbilities honours the chosen Plan::dig_choice) -- one reader, so the
// rollout and the executor cannot disagree about which world they are in.
inline bool DigSearchedChoiceOn()
{
    static const bool env_on = EnvOn("MTG_DIG_SEARCHED_CHOICE", true);
    return heurarm::Flag(heurarm::DIG_SEARCHED_CHOICE, env_on);
}

// MTG_TH_DIG_PRUNE (DEFAULT ON; =0 -> treasure_hunt gets the no-dig twin like every other deck) -- read by
// TreasureHuntProvider::DigOfferAt, the provider's prune of its own dig decision to its gate.
inline bool TreasureHuntDigPruneOn()
{
    static const bool env_on = EnvOn("MTG_TH_DIG_PRUNE", true);
    return heurarm::Flag(heurarm::TH_DIG_PRUNE, env_on);
}

// MTG_DRAW_HELD_DROP_REPLAY (DEFAULT ON; =0 -> a drop held for a draw engine that the main never cast is
// lost, byte-identical to the tree before 2026-10-14) -- read by AIEngine::TakeTurn's depth-0 post-cast
// replay of the drop DecisionProvider::LandDropAfterDrawEngine held. Executor-only (the rule it completes
// is a depth-0 executor rule); heurarm slot so one pooled batch carries both arms.
inline bool DrawHeldDropReplayOn()
{
    static const bool env_on = EnvOn("MTG_DRAW_HELD_DROP_REPLAY", true);
    return heurarm::Flag(heurarm::DRAW_HELD_DROP_REPLAY, env_on);
}

// MTG_SPECTACLE_PLAN_CREDIT -- the SPECTACLE PLAN CREDIT (docs/design/burn-cast-order-2026-10-15.md,
// part 3). Light Up the Stage is {2}{R} until an opponent has lost life this turn and {R} after
// (CR 702.137a: spectacle is an alternative cost, chosen at 601.2b and legal iff an opponent lost
// life this turn). Both subset evaluators (Solve::consider -- the d0 runner and every rollout leaf --
// and EnumeratePlans::eval_and_push, the search) priced it at the cost of the state BEFORE the plan,
// so {Swiftspear, a burn spell, Light Up} read as five mana on three lands and was never a plan
// (burn seed 9295 gi 294: a turn-3 kill missed at every budget). With this on, the plan's spectacle
// cast is priced at its spectacle cost when the same plan casts a FACE-DAMAGE spell that the realised
// order puts before it (OpaqueCastOrderActive + CastOrderLess, or a sac-land burn the apply casts
// first), and the odometer's scalar mana bound is loosened by the same amount. A faithful rules credit
// (affordability), not a heuristic: the apply really does cast the burn first and pays {R}. Read by
// the two evaluators only (TurnSolver.cpp); the executor's own payment recomputes the live cost.
// Only a deck holding a spectacle card can move (Light Up the Stage: burn alone). Per-job heurarm slot.
inline bool SpectaclePlanCreditOn()
{
    static const bool env_on = EnvOn("MTG_SPECTACLE_PLAN_CREDIT", true);   // DEFAULT ON (measured 2026-10-15); =0 disables
    return heurarm::Flag(heurarm::SPECTACLE_PLAN_CREDIT, env_on);
}

// MTG_KILL_TRIGGER_COMBO -- the "wins this turn" projection of both subset evaluators credits a
// kill-trigger burn spell's face damage (Searing Blood: "When that creature dies this turn, Searing Blood
// deals 3 damage to the creature's controller") when the PLAN's creature damage, in the realised cast
// order, kills the opponent's ONLY creature -- a Blood that cannot kill alone (a 3/3, a 4/4) finished by
// a second Blood or a Searing Blaze cast after it. CollectActions credits a Blood only when it kills
// ALONE, so such a plan read as short of lethal, and the Shard Volley hold's exception (a) ("the plan wins
// this turn") then pruned the Volley that was the kill. It surfaced under burn's main-2 rule: the same
// Volley cast in main 2 sees the board after the triggers resolved, but the rule moves it to main 1, where
// only the projection can see it (burn2hg seeds 11671958 gi 1957, 11671187 gi 1186: not recovered at d8 b0
// before this). A projection-accuracy fix, read by the two evaluators only; the apply already resolves the
// trigger. One opposing creature only (every creature-damage spell then hits it); 0 -> byte-identical.
// Default ON (measured 2026-10-15); per-job heurarm slot.
inline bool KillTriggerComboOn()
{
    static const bool env_on = EnvOn("MTG_KILL_TRIGGER_COMBO", true);   // DEFAULT ON (measured 2026-10-15); =0 disables
    return heurarm::Flag(heurarm::KILL_TRIGGER_COMBO, env_on);
}

// MTG_SPECTACLE_CREDIT_SEARCH_ONLY -- the credit above in the SEARCH's enumeration only (EnumeratePlans),
// not in the greedy walk (Solve: the d0 runner and every rollout leaf), which keeps the pre-plan cost --
// the asymmetry Plan B always had (it is EnumeratePlans-only). Why it exists: in the greedy walk the credit
// newly admits {Shard Volley, Light Up the Stage {R}} in main 1, and the Volley hold's exception (b) ("it
// turns Spectacle on for a spell in the same plan", shard-volley-hold.md) lets the greedy sacrifice a land
// on T2-T3 for it where it used to keep Volley and cast Light Up for {R} in main 2 off combat damage (d0
// probe, 1500 games: 49 faster / 39 slower). The SEARCH prices that land; the greedy does not. A measured
// alternative, not a fix of a rules defect (the credit is faithful either way). Default OFF.
// Read by the evaluators and by BurnProvider's d0 land hold (which must agree with what Solve can find).
inline bool SpectacleCreditSearchOnly()
{
    static const bool env_on = EnvOn("MTG_SPECTACLE_CREDIT_SEARCH_ONLY");   // DEFAULT OFF; =1 enables
    return heurarm::Flag(heurarm::SPECTACLE_CREDIT_SEARCH_ONLY, env_on);
}

// MTG_ATTACK_BODY_TAP_ORDER. The attack-turn creature tap order: see
// DecisionProvider::ManaSourceHoldValue. Read inside the shared payment (TapForCostSharedOnce);
// heurarm slot so a pooled batch can carry both arms.
//
// DEFAULT: ON IN HUMAN PLAY, OFF AUTONOMOUSLY (2026-10-06). In the viewer the engine pays the
// human's line, and a payment that taps a creature the human is about to attack with silently
// costs them damage they never chose to give up -- Mirrorwing v2 s51_gi50 T4 (USER-saved reference):
// Luxurious Libation X=5 tapped both 1-power Mystics over two 0-power Hierarchs, 19 damage against
// the recorded 21 kill. USER 2026-10-06: a reference that stops replaying is ours to fix. Autonomous
// play keeps it OFF until the held-out A/B the USER calls for (regression evidence so far: 6 games
// faster / 0 slower of 56,060), so no GT moves. HumanPlayActive() is false inside rollouts, so the
// search arm is unchanged too. An explicit MTG_ATTACK_BODY_TAP_ORDER=0/1 overrides both defaults.
inline bool AttackBodyTapOrderOn(bool human_play)
{
    static const bool explicit_set = EnvSet("MTG_ATTACK_BODY_TAP_ORDER");
    static const bool env_on = EnvOn("MTG_ATTACK_BODY_TAP_ORDER");
    return heurarm::Flag(heurarm::ATTACK_BODY_TAP_ORDER, explicit_set ? env_on : human_play);
}

// MTG_SOLVE_COMBAT_SWAP -- DEFAULT ON; =0 reverts. The greedy policy's Aura-swap TIMING (USER
// 2026-10-06): Solve drops a pre-combat Arcanum Wings swap that would bring a host-tapping Aura onto
// a creature that could attack (the swap belongs in combat, after attackers are declared) when the
// combat swap stays payable without tapping an attacker, and the d0 runner pins that combat swap. Read by TurnSolver (Solve) and AIEngine (the d0 pin): one reader.
inline bool SolveCombatSwapOn()
{
    static const bool env_on = EnvOn("MTG_SOLVE_COMBAT_SWAP", true);
    return heurarm::Flag(heurarm::SOLVE_COMBAT_SWAP, env_on);
}

// MTG_BOUNCE_UNTAPPED_FIRST -- DEFAULT ON (ADOPTED 2026-10-08 with MTG_BOUNCE_SEARCH below, after the
// held-out A/B in docs/design/karoo-tap-in-response.md; =0 restores the old order with no float).
// The USER chose the model on 2026-10-06. ONE lever, two halves
// that are only sound TOGETHER, so they are measured jointly:
//   (1) TAP IN RESPONSE: a Karoo's "return a land you control to its owner's hand" is a triggered
//       ability (CR 603.2), so before it resolves the controller holds priority and may activate the
//       returned land's mana ability (CR 605.3a); the mana stays in the pool until the step/phase ends
//       (CR 106.4). BounceKarooLand models it: an UNTAPPED, side-effect-free returned land is tapped
//       into state.floating_mana first (KarooBounceFloatable / FloatKarooBouncedLand, SpellEffects.h).
//   (2) THE USER'S ORDER: with (1), returning an untapped land costs nothing this turn, so
//       DecisionProvider::BounceLandCandidates prefers a land that RE-ENTERS UNTAPPED over an
//       already-tapped one that re-enters tapped. USER: "Generally the Orchard is better to bounce
//       because the monastery will come into play tapped again."
// (2) alone was measured first and REGRESSED (Hinata d0 s2002 gi522, 7 -> 8: a mid-turn Karoo returned
// an untapped land whose mana Hinata still needed); USER 2026-10-06: "I recommend modelling tapping the
// land in response." Read by BounceKarooLand (the ONE land-drop ETB shared by executor, rollout and
// enumeration probe) and by the provider -- one reader so the two halves can never disagree. heurarm
// slot so a pooled batch carries both arms.
inline bool KarooTapInResponseOn()
{
    static const bool env_on = EnvOn("MTG_BOUNCE_UNTAPPED_FIRST", true);   // DEFAULT ON; =0 reverts
    // REPLAY of a reference recorded before the Karoo rules (no `karoo_float` stamp ->
    // --legacy-karoo-float, or its env twin MTG_HUMAN_KAROO_FLOAT=0): the game was played with no
    // float AND the old bounce order as the prompt's default, so both halves are off for it. Human
    // play only -- autonomous play never reads the legacy gate.
    if (HumanPlayActive() && !HumanKarooFloatRuleOn()) { return false; }
    return heurarm::Flag(heurarm::BOUNCE_UNTAPPED_FIRST, env_on);
}

// MTG_BOUNCE_SEARCH -- DEFAULT ON (ADOPTED 2026-10-08 with the lever above). The Karoo bounce as a SEARCHED plan dimension (USER 2026-10-08: "The Karoo
// decision should also be searched with heuristics. However, we may want to heuristic it more often
// than not, since the decision is usually an easy one."). BounceKarooLand used to take
// BounceLandCandidates().front() at every node -- a provider top pick taken without branching inside
// the search window, which the no-greedy rule forbids; the held-out A/B of the lever found the game
// it costs (Dragons s4004 gi47, docs/design/karoo-tap-in-response.md). On: the provider narrows the
// returnable lands (DecisionProvider::BounceSearchCandidates, one candidate in the easy cases), the
// enumeration emits one plan per further candidate (Plan::bounce_choice, AppendSubdecisionAxes), and
// both apply worlds pin it (ScriptedBounceChoice). The d0 runner and the playout beyond the horizon
// keep the front pick (they carry no pin). Read by the enumeration and BounceKarooLand; heurarm slot
// so a pooled batch carries both arms.
// MTG_BOUNCE_FOLD -- DEFAULT ON; =0 restores the post-apply-only dedup. The searched bounce's
// enumeration can only BOUND how many candidates a plan's bounce will offer (which lands are spent is
// known to the apply), so it emits variants whose index the bounce state then clamps onto a sibling's
// candidate (53% of them on the held-out run). The candidate loops learn the real width from the first
// sibling they apply (BounceProbe, SpellEffects.h) and decline every later variant that resolves to a
// candidate a sibling already applied -- BEFORE its unit charge and its apply, exactly as the wave-0
// stillborn / collapse skips do. SOUND by identity: the variant is a clone of that sibling differing
// only in bounce_choice, both resolve the bounce to the same land on the same state, so the apply is
// the same apply. MTG_BOUNCE_FOLD_VERIFY applies the variant anyway and checks the post-apply key.
// MTG_BOUNCE_USER_RULE -- the USER's Karoo bounce default (2026-10-08, two refinements the same day):
// "I guess we should implement some default heuristic for Karoo bounces. We don't need to search if
// there is a tapped land that comes into play untapped [...]" / "I mean that it doesn't generate
// colours any other land doesn't." A returnable land that is TAPPED, re-enters UNTAPPED on its replay,
// and makes no colour that no other land we keep makes, is returned with no search
// (DecisionProvider::BounceUserRulePick) -- AMENDED by MTG_BOUNCE_RULE_AMEND below. DEFAULT ON
// (2026-10-09, with the amendment and MTG_BOUNCE_REPLAY_PRECISE): held-out vs the tip, every Karoo
// deck 0 except melira -23 / mirrorwing -26 (the precise order's d0 games), 0 searched games slower,
// all 46 slower d0 games recover (docs/design/karoo-tap-in-response.md). Rides the lever: in a legacy
// replay (no `karoo_float` stamp) the lever is off in human play, so an old recording is untouched.
inline bool BounceUserRuleOn()
{
    static const bool env_on = EnvOn("MTG_BOUNCE_USER_RULE", true);
    return heurarm::Flag(heurarm::BOUNCE_USER_RULE, env_on) && KarooTapInResponseOn();
}

// MTG_BOUNCE_RULE_AMEND -- the base rule's amendment (DecisionProvider::BounceUserRulePick), DEFAULT ON.
// Root cause (Dragons s7007 gi959): Haven of the Spirit Dragon's creature-only rainbow counted as real
// colours, so a kept Haven "covered" everything and the rule returned the tapped Mountain -- a T6 board
// even for an unbounded depth-8 search, against T5 with the Haven returned. (1) Creature-only colours are
// not colours, on either side of the coverage test. (2) The rule decides only when ONE land qualifies;
// two different qualifying lands are a real choice (gi888 needed the Mountain returned with the same two
// lands on offer) and go to the search. =0 restores the unamended rule.
inline bool BounceRuleAmendOn()
{
    static const bool env_on = EnvOn("MTG_BOUNCE_RULE_AMEND", true);
    return heurarm::Flag(heurarm::BOUNCE_RULE_AMEND, env_on);
}

// MTG_BOUNCE_REPLAY_PRECISE -- the bounce order and the search narrowing read a returned land's re-entry
// off the board (BounceReplayEntersUntapped: a reveal land with nothing to reveal, a fastland past its
// count, a checkland without its type re-enter TAPPED) rather than off enters_tapped alone. DEFAULT ON
// (2026-10-09): held-out melira -23, mirrorwing -26, every other deck 0; =0 restores the param read.
inline bool BounceReplayPreciseOn()
{
    static const bool env_on = EnvOn("MTG_BOUNCE_REPLAY_PRECISE", true);
    return heurarm::Flag(heurarm::BOUNCE_REPLAY_PRECISE, env_on);
}

// MTG_HINATA_BOUNCE_RULE -- the USER's Hinata Karoo rule (HinataProvider::BounceUserRulePick; live
// through HinataProvider::BounceUserRuleActive whatever MTG_BOUNCE_USER_RULE says). DEFAULT ON
// (2026-10-08): held-out (overnight) vs the tip, Hinata -30 turns over 10,800 games (42 faster / 18
// slower), its 2HG rows -7 over 400 (4 / 0); every searched slower game recovers at both escalation
// stages. Read only by that override (and the MTG_BOUNCE_STATS census).
inline bool HinataBounceRuleOn()
{
    static const bool env_on = EnvOn("MTG_HINATA_BOUNCE_RULE", true);
    return heurarm::Flag(heurarm::HINATA_BOUNCE_RULE, env_on);
}

inline bool BounceFoldOn()
{
    static const bool env_on = EnvOn("MTG_BOUNCE_FOLD", true);
    return heurarm::Flag(heurarm::BOUNCE_FOLD, env_on);
}
inline bool BounceFoldVerifyOn()
{
    static const bool on = EnvOn("MTG_BOUNCE_FOLD_VERIFY");
    return on;
}

inline bool KarooBounceSearchOn()
{
    static const bool env_on = EnvOn("MTG_BOUNCE_SEARCH", true);   // DEFAULT ON; =0 = the front() pick
    return heurarm::Flag(heurarm::BOUNCE_SEARCH, env_on);
}

// MTG_KAROO_POSITION -- DEFAULT ON; =0 restores "a Karoo drop is always played after the casts". The
// Karoo drop's POSITION in the cast sequence is a searched choice (USER 2026-10-22: "We should allow
// the sanctuary to be played at the end of the phase at latest"). The deferral (karoo_deferred: play it
// after the casts, so its mandatory bounce returns a land the casts already spent) stays the default
// and is always enumerated; this adds the EARLIER positions -- the drop right before a cast that can
// observe the land entering (Rest for the Weary's landfall 8, Searing Blaze's landfall 3, a landfall
// trigger whose payload a later cast reads) -- as clones of the Karoo plan (Plan::land_slot <= -2,
// TurnSolver::KarooSlotBefore). An untapped returned land is tapped in response
// (MTG_BOUNCE_UNTAPPED_FIRST's float), so "tap the Forest, play Sanctuary returning it, cast Rest with
// the floating {G}" is the same line a human plays. Read by the enumeration only (the applies follow
// whatever position a plan carries, so a committed plan is realised identically either way); human play
// offers every position regardless (the viewer never narrows). heurarm slot so a pooled batch carries
// both arms.
inline bool KarooPositionSearchOn()
{
    static const bool env_on = EnvOn("MTG_KAROO_POSITION", true);   // DEFAULT ON; =0 = after the casts only
    return heurarm::Flag(heurarm::KAROO_POSITION, env_on);
}

// MTG_NEEDS_TAP_ORDER -- DEFAULT OFF until measured. Needs-based mana-source choice: see the
// ComputeNeedsDemandSupply header in ManaPayment.cpp. Read inside the shared payment, so executor
// and rollout see one value; heurarm slot so a pooled batch can carry both arms.
inline bool NeedsTapOrderOn()
{
    static const bool env_on = EnvOn("MTG_NEEDS_TAP_ORDER");
    return heurarm::Flag(heurarm::NEEDS_TAP_ORDER, env_on);
}

// MTG_RESTRICTED_MANA_RANK -- DEFAULT ON; =0 restores the historical ladder. A colored_creature_only
// source (Haven of the Spirit Dragon, Cavern of Souls, Unclaimed Territory, ...) whose RESTRICTED
// colours this deck's creature spells can use number at most ONE is ranked as what it is: a {C}
// source plus at most one creature-only colour -- below the mono tier, so it pays (generic, and the
// coloured pip of a creature spell) before an unrestricted land that makes the same colour, which can
// do everything it can and more. The historical ladder read its six-entry produces list as a rainbow
// and held it LAST of the lands (59). USER 2026-10-08 on payment: "if there was something else we
// could have tapped for mana then we should do so". Dragons s7007 gi959: Atsushi paid with a Mountain
// beside an untapped Haven, one firebreath short of the T5 kill. Two companions ride the same lever,
// because the earlier tap now reaches the Haven's OWN activation (its "{2}, {T}, sacrifice" rebuy): the
// activation's {T} source is held through the plan's cast payments (ComputePlanTraits' act-line hold)
// and through its own {2} (a source cannot pay mana toward an ability whose {T} it owes). Read by the
// shared provider rank, the plan traits and both rebuy apply sites (executor + rollout lockstep).
// heurarm slot so a pooled batch carries both arms.
inline bool RestrictedManaRankOn()
{
    static const bool env_on = EnvOn("MTG_RESTRICTED_MANA_RANK", true);
    return heurarm::Flag(heurarm::RESTRICTED_MANA_RANK, env_on);
}

// MTG_PREPAY_FLOAT_POOL -- DEFAULT ON; =0 restores the PP_FLOAT_NZ decline. The whole-turn prepay
// (TurnSolver::BatchPrepayMainCasts, the one function both apply worlds call) starts its joint solve
// from a PLAIN floating pool instead of declining on it. See the block at the decline site.
inline bool PrepayFloatPoolOn()
{
    static const bool env_on = EnvOn("MTG_PREPAY_FLOAT_POOL", true);
    return heurarm::Flag(heurarm::PREPAY_FLOAT_POOL, env_on);
}

// MTG_RAMP_FEED_ROUTE -- DEFAULT ON; =0 restores the feed-blind greedy. The shared per-cast payer
// re-pays a cast whose greedy spent the last feeder of an untapped ramp filter, routing that feeder
// through the filter, when only the routed board still pays the rest of the line (RampFeedRouteRepay,
// ManaPayment.cpp). Executor and rollout share the payer, so the two stay in lockstep.
inline bool RampFeedRouteOn()
{
    static const bool env_on = EnvOn("MTG_RAMP_FEED_ROUTE", true);
    return heurarm::Flag(heurarm::RAMP_FEED_ROUTE, env_on);
}

// MTG_PAY_ROLLBACK -- DEFAULT OFF until measured. The same-turn mana-payment rollback: see
// src/ai/PayRollback.h. Read in the shared payment entry and both dig-gate twins (lockstep).
inline bool PayRollbackOn()
{
    static const bool env_on = EnvOn("MTG_PAY_ROLLBACK");
    return heurarm::Flag(heurarm::PAY_ROLLBACK, env_on);
}

// MTG_ETB_WATCHER_GATES (DEFAULT ON) -- stamp the two remaining ETB-cascade presence gates
// (GameState::deck_has_subtype_enter_counters / deck_has_etb_counter_payer) from the decklist
// instead of leaving them unconditionally open.
//
// THIS IS NOT A HEURISTIC AND IT NARROWS NOTHING. The scans it gates look for a CardParams field;
// when no card in the mainboard or sideboard carries that field, the scan is guaranteed to find
// nothing, so skipping it cannot change a single trigger. It is behind a lever purely so the COST
// can be measured as a proper A/B in ONE pooled batch (the repo forbids the per-arm wave a
// process-wide static would force), and so the old behaviour stays one flag away if the stamp is
// ever found to be wrong for a deck.
//
// WHY IT DEFAULTS ON, unlike the narrowing levers above it: the three sibling gates added
// 2026-09-17 (ascend / devotion / dragon_ping) also ship unconditionally on, for the same reason --
// a gate that is provably inert cannot be "adopted", only switched off by accident. Turning it off
// restores a scan that finds nothing; the measured effect is wall, not play.
//
// SAFETY, which is the whole argument: the flags DEFAULT TRUE in GameState (= do the scan = old
// behaviour), so any path that builds a state WITHOUT stamping -- the scenario harness is the
// documented one -- keeps every trigger. A flag wrongly true costs time; wrongly false drops a
// trigger. Only ever err true. Garth One-Eye holds every gate open because he materialises cards
// that need not appear in any decklist.
//
// Read at STAMP time only (GoldFishRunner::StampDeckTraits), so there is no executor/rollout
// lockstep concern: the stamp is a per-GAME constant and both worlds read the same GameState field.
inline bool EtbWatcherGatesEnabled()
{
    static const bool env_on = EnvOn("MTG_ETB_WATCHER_GATES", true);
    return heurarm::Flag(heurarm::ETB_WATCHER_GATES, env_on);
}

// MTG_ENTER_WATCHER_GATE (DEFAULT ON) -- the same family as the lever above, given its OWN slot so
// it can be A/B'd alone. It stamps GameState::deck_has_creature_enter_watcher, which gates
// FireCreatureEnterWatchers' own top-level cascade loop.
//
// SEPARATE FROM MTG_ETB_WATCHER_GATES ON PURPOSE. That lever covers five gates at once, so using it
// as this one's control arm would have measured six changes and attributed them to one. The repo's
// A/B rule wants the arms INTERLEAVED in a single pooled batch, which needs a per-job heurarm slot
// rather than a process-wide static -- hence the slot.
//
// IT NARROWS NOTHING, and the argument is stronger than the sibling gates': those reason "no card
// carries the param, so the scan finds nothing". This one is stamped from DefHasCreatureEnterWatcher
// -- the SAME disjunction CardDefinition::enter_watcher is computed from, which is the exact
// predicate the loop tests per permanent. So the flag being false and every iteration answering "no"
// are not two facts that happen to agree; they are one fact. Byte-identical by construction.
//
// WHY IT EXISTS AT ALL, given the loop was already tight: def_absent + the folded `enter_watcher`
// byte had driven the per-permanent cost down to about as low as it goes, and the site STILL profiled
// at 12.20% self on candidate-B Fungus -- because the residue is the TRIP COUNT, not the body.
// CreateToken routes through the cascade, so a token deck pays O(tokens x board) to ask a question
// whose answer was fixed when the decklist was read.
//
// Read at STAMP time only (GoldFishRunner::StampDeckTraits), exactly like the lever above: the stamp
// is a per-GAME constant, so executor and rollout read one GameState field and cannot drift.
inline bool EnterWatcherGateEnabled()
{
    static const bool env_on = EnvOn("MTG_ENTER_WATCHER_GATE", true);
    return heurarm::Flag(heurarm::ENTER_WATCHER_GATE, env_on);
}

// MTG_SAC_FODDER_SAME_LINE -- ADOPTED 2026-09-24, DEFAULT ON (=0 restores the old enumeration, which
// is the only way to reproduce a pre-adoption ground truth). Let a sac outlet be paid with fodder
// THIS SAME LINE creates. See SameLineSacFodderSource (core/SpellEffects.h) for the mechanism and
// docs/design/sac-fodder-created-in-the-same-line.md for the three user reports behind it.
//
// THIS IS A MISSING LINE, NOT A HEURISTIC, and that changes what the measurement had to show. The
// off arm is not a defensible alternative policy -- it is simply unable to express a rules-legal
// play (three idle spore counters become a Saproling, that Saproling becomes a mana). So the bar was
// not "is it better", it was "does restoring the line cost anything". It does not:
//
//   Goblins        3.8033 -> 3.8033   IDENTICAL DIGEST (unaffected by construction, see below)
//   Fungus         5.4683 -> 5.4583   train, 600 games
//   Fungus         5.4170 -> 5.3998   HELD OUT, 4000 games, disjoint seeds -- the sign confirms
//   candidate B    5.5033 -> 5.4950   train, 600 games
//
// (lower avg win turn is better; every arm paired on the same seeds.)
//
// WHY GOBLINS IS UNTOUCHED, and it is by construction rather than by luck: the fodder maker must be
// a COUNTER-costed token ability (spore/fade). Skirk Prospector's feeder is Krenko, whose token
// ability costs {T} -- outside that set -- so no Goblins action changes. The September write-up
// expected this fix to move Goblins and asked for its own regression cycle on that basis; scoping
// the maker set is what removed the need.
//
// IT COSTS WALL, and that is the honest trade: 4000 held-out Fungus games ran +12.3% (821,746 ->
// 922,784 ms). More legal actions means a wider enumeration. Paid for by a real line the search
// could not previously reach.
//
// Read at EMISSION time only, so there is no executor/rollout lockstep concern: with the lever off
// no action carrying kSameLineSacVictim is ever created, and the apply-side fusion is unreachable.
inline bool SacFodderSameLineEnabled()
{
    static const bool env_on = EnvOn("MTG_SAC_FODDER_SAME_LINE", true);
    return heurarm::Flag(heurarm::SAC_FODDER_SAME_LINE, env_on);
}

// ---- THE TWO WIDENINGS (USER 2026-09-24: "We should probably not restrict it at all") ----------
// The adopted cut above restricts same-line fodder twice. Each widening gets its OWN lever because
// they have different blast radii and must be adoptable independently -- one lever covering both
// would force an all-or-nothing call on two changes whose evidence is separate.

// MTG_SAC_FODDER_VALUE_OUTLET (DEFAULT OFF -> byte-identical) -- same-line fodder for a VALUE outlet,
// not just a mana one: Psychotrope Thallid's "{1}, Sacrifice a Saproling: Draw a card", and the
// Deathspore / Vitaspore sac payloads. Needs the fusion in ApplySacCreatureOutlet as well, which is
// a different function from ApplySacForMana -- so this is a second apply site, not just a widened
// predicate. Expected to be Fungus-local: no other suite deck owns a free token maker AND a
// subtype-filtered value outlet.
inline bool SacFodderValueOutletEnabled()
{
    static const bool env_on = EnvOn("MTG_SAC_FODDER_VALUE_OUTLET");
    return heurarm::Flag(heurarm::SAC_FODDER_VALUE_OUTLET, env_on);
}

// MTG_SAC_FODDER_TAP_MAKER (DEFAULT OFF -> byte-identical) -- let a {T}-costed token maker supply the
// fodder, which is Krenko, Mob Boss feeding Skirk Prospector. THIS ONE MOVES GOBLINS, and that is
// the whole reason it is separate: the adopted cut leaves Goblins byte-identical because Krenko was
// out of scope, and the original design doc expected this fix to need its own Goblins rebaseline.
// It does -- just here rather than there.
//
// The cost is REAL in a way the counter-costed makers' is not: spore/fade counters have no other use
// this turn, but tapping Krenko forfeits whatever else that {T} could have bought, and the
// enumerator may have planned to spend it. Hence searched LAST in SameLineSacFodderSource.
inline bool SacFodderTapMakerEnabled()
{
    static const bool env_on = EnvOn("MTG_SAC_FODDER_TAP_MAKER");
    return heurarm::Flag(heurarm::SAC_FODDER_TAP_MAKER, env_on);
}

// MTG_SAC_FODDER_ACT_MAKER -- DEFAULT ON (=0 restores the old enumeration exactly). Fodder made by a
// MANA-COSTED token ability the same line activates: Slimefoot, the Stowaway's "{4}: Create a 1/1
// Saproling", then Utopia Mycon (or a Thallid value outlet) sacrificing that Saproling -- the ping
// line, "cast Slimefoot, make a Saproling, sac it for the last point of damage" (FungusB, Soldiers
// ledger 2026-10-04).
//
// A MISSING LINE, NOT A HEURISTIC, so this ships default-on like MTG_SAC_FODDER_SAME_LINE did. The
// free makers (spore/fade) are FUSED into the sac (SameLineSacFodderSource); a mana-costed maker
// cannot be, because its cost would be paid inside a pre-cast SacForMana the enumerator had already
// credited. So the outlet is emitted in its TRAILING form (Kind::SacCreatureOutlet, victim
// kSameLineSacVictim), the maker stays its own searched ActivatePermAbility, and both trailing
// dispatchers run the sentinel sac AFTER every other activation of the pass
// (TurnSolver::DeferSameLineSacs) -- so the token exists when the outlet looks for it, and the
// outlet's mana (if any) floats after the casts and is credited to nothing. Read at EMISSION only:
// with it off no such action exists and the dispatcher reorder is a no-op.
//
// WHY THE JUST-CAST CASE NEEDED NOTHING ELSE: a Slimefoot cast this turn reaches its {4} through
// breakpoint site 9's continuation, which is enumerated by the same CollectActions -- so the
// continuation [{4} token, sac it] is one candidate of the one site-9 occurrence. The previous
// diagnosis ("site 9 fires once per apply, so the token cannot open a second occurrence") was a
// symptom: a Slimefoot ALREADY on the battlefield could not find the line either.
inline bool SacFodderActMakerEnabled()
{
    static const bool env_on = EnvOn("MTG_SAC_FODDER_ACT_MAKER", true);   // DEFAULT ON; =0 disables
    return env_on;
}

// MTG_M2_FIXPOINT (DEFAULT OFF -> byte-identical; heurarm slot for per-job pooling): restore the
// FREE INTER-MAIN RE-SOLVE the second main never had -- after an m2 plan whose apply/execution
// FIRED a breakpoint (cards may have entered hand mid-plan), solve m2 AGAIN on the post-draw
// state, loop-capped. Read by BOTH sides, which is why it lives here (lockstep rule):
//   * search scoring -- FSLineTail's m2 loop recursion, and the interior
//     SolveSecondMainInSearch apply sites (SolveWithLookahead / SimulateToEnd), so rollouts
//     price turns the way real play will play them;
//   * the executor -- GameEngine::MainPhase re-enters TakeTurn on the same condition
//     (AIEngine::WantsSecondMainReentry), so the realized turn matches the scored shape.
// Measured motivation: hinata gi=66 ends its second main with six untapped sources and lethal
// in hand at ANY budget (searched-second-main-unconditional.md, "the split forfeits the free
// inter-main re-solve"); base play only ever gets this re-solve at the m1->m2 boundary.
inline bool M2FixpointEnabled()
{
    static const bool env_on = EnvOn("MTG_M2_FIXPOINT");
    return heurarm::Flag(heurarm::M2_FIXPOINT, env_on)
        || heurarm::Flag(heurarm::M2_FIX_RESOLVE, false);
}
// Fixpoint MODE (USER direction 2026-09-06: "we need to re-evaluate" at the post-draw points;
// condemnation prunes WHAT gets re-evaluated, not WHETHER):
//   1 = KILL-SCAN only (probe projected-lethal post-draw plans, commit on a verified win).
//   2 = kill-scan PLUS a gated full RE-SOLVE: when a card drawn during the plan's execution is
//       ACTIONABLE (some enumerated post-draw plan uses it), re-solve the remainder of the
//       second main and play/score that line. The gate is NEW-INFORMATION CONDEMNATION -- the
//       pre-draw solve already adjudicated the old hand, so a re-solve is condemned unless a
//       drawn card appears in some plan. This is v2's consistent re-evaluation with the two
//       rejections addressed: cost (v2 re-solved on EVERY draw-firing rollout m2; mode 2 only
//       on actionable draws) and the d0 hole (the executor path is depth-independent now).
// MTG_M2_FIXPOINT=2 selects mode 2 (EnvInt; =1 or the heurarm M2_FIXPOINT flag = mode 1;
// heurarm M2_FIX_RESOLVE = mode 2 for pooled per-job sweeps).
inline int M2FixpointMode()
{
    static const int env_mode = EnvInt("MTG_M2_FIXPOINT", 0);
    if (heurarm::Flag(heurarm::M2_FIX_RESOLVE, env_mode >= 2)) { return 2; }
    return M2FixpointEnabled() ? 1 : 0;
}
// Nesting/iteration cap for the fixpoint (a chain of draw-firing plans re-enters once per pass).
// Budget bounds the search-side work regardless; the cap exists so an adversarial draw chain
// cannot recurse or re-enter without bound on an unbudgeted run.
inline int M2FixpointCap()
{
    static const int cap = std::max(1, EnvInt("MTG_M2_FIXPOINT_CAP", 2));
    return cap;
}

// MTG_M2_RECONSIDER -- ADOPTED DEFAULT-ON 2026-08-29 (USER: "okay iff it is a strict
// improvement"; =0 restores the old behaviour): main 2 RECONSIDERS newly-available cards,
// including the land drop (USER rule 2026-08-26: "all we need to do is allow for
// reconsideration of drawn/staged cards including the land drop" -- NOT a spectacle/deck
// special case). Unlike MTG_MAIN2_DROP (blanket m2 land dimension, measured to bloat hinata's
// plan space), this opens the m2 land drop ONLY at states where a STAGED land sits in hand --
// a land main-1 could not have planned around, e.g. impulse-exiled by a post-combat spectacle
// Light Up the Stage. Adoption evidence met the strict bar: train regression -- only burn
// moved, every mover faster (incl. two wins the blanket flag never found), zero slower;
// held-out overnight -- burn faster in 8/8 cells (incl. an unwon game -> T8 win), zero slower
// anywhere, every other deck byte-identical. Read by the search (M2DropLive in TurnSolver)
// and the executor (AIEngine fold_land + ApplyPlanDirect's plan.land_decided follow) --
// shared reader per the lockstep rule.
inline bool M2ReconsiderEnabled()
{
    static const bool v = EnvOn("MTG_M2_RECONSIDER", true);
    return v;
}

// MTG_HINATA_SUBSET_CREDIT=1 -- measurement lever (DEFAULT OFF): the SAME-SUBSET Hinata discount
// credit. Root cause it exists for (searched-design-deck-rollout.md §6, 2026-08-30): greedy
// re-prices SEQUENTIALLY while every searched form prices plans STATICALLY at enumeration, and
// hinata_cost_reducer had no same-subset credit -- so a one-enumeration plan {Hinata, Reality
// Spasm.., payoff} priced the chain at full cost and was never emitted. This is why greedy beat
// every searched form on the deck, why all-main-2 fails (the m1/m2 boundary was the search's only
// free re-pricing point -- gi=22), and why MTG_BP_NODE lost at equal compute. Under the lever:
// (a) the untap ritual is EMITTED when she is castable from hand (honest, undiscounted cost);
// (b) the X-payoff's max-X range is sized as if she resolves first; (c) the enumerator's subset
// gate credits her would-be discount for subsets that actually cast her (metalcraft-credit
// precedent: optimism where the apply validates -- she casts first by CastOrderRank in both
// worlds, and the per-cast payment recomputes, so the credited discount is realised, never
// stranded; the batch prepay declines on X-spells so it cannot fix costs early). ENUMERATOR
// ONLY, never the d0 leaf (the LeafReducerCreditEnabled law). Read by the provider gate
// (DecisionProviders: ShouldEmitUntapRitual) and the solver (TurnSolver: sizing + credit) --
// shared reader per the lockstep rule.
inline bool HinataSubsetCreditEnabled()
{
    static const bool env_on = EnvOn("MTG_HINATA_SUBSET_CREDIT");
    return heurarm::Flag(heurarm::HINATA_SUBSET_CREDIT, env_on);
}

// MTG_FB_TAP -- firebreathing pumps TAP REAL SOURCES for what they spend (default ON; =0 restores
// the read-only pool). The historical model read AvailableManaPool without tapping, justified as
// "goldfish combat is the turn's LAST mana use" -- an invariant the uses_second_main adoption
// broke: a post-combat main re-reads AvailableManaPool from the same untapped lands, so pump mana
// double-spent (dragons gi29 T5: the SAME casts in both arms, but the m2 arm's Bolt {R} both
// pumped Inferno in combat AND paid for the Bolt after -- an illegal +1 damage at exact lethal;
// 39 of the m2 rule's 43 dragons wins were this class, all d8b0-structural because no LEGAL line
// reaches them). Both worlds (AIEngine::Firebreathe + the rollout's SimulateCombat site) tap via
// the same shared payment after ApplyFirebreathing reports its spend -- lockstep rule.
inline bool FirebreatheTapsEnabled()
{
    static const bool v = EnvOn("MTG_FB_TAP", true);
    return v;
}

// MTG_EXEC_FEAS -- EXECUTOR-VALIDATED sequential subset payability.
// >>> DEFAULT ON since 2026-09-04 (=0 restores the flat gates); it was default OFF from its
// 2026-08-30 build (never-worse over 12,000 paired games, unit cost unmeasured) until the
// dragons second-main retirement REQUIRED it: retiring m2 without it re-opens the gi94
// producer-chain unreachability (Sol Ring -> Mind Stone same-main), violating the
// no-strictly-better-line-dropped bar. Adopted as part of the FB_TAP/SUBRED_BAIL/m2-retirement
// package; the suite is the gate. PERF NOTE (2026-09-04): its unit cost has still never been
// isolated (suite makespans stayed normal -- regression ~3 min, overnight ~30 min -- so no alarm);
// on the next perf pass, wall-probe MTG_EXEC_FEAS=0 vs =1 per wall_probe.sh before profiling
// anything downstream of the enumerator.
// The enumerator's three mana gates (flat pool, colour-exists, colour-exact) price a subset's whole
// cost against the pre-cast board SIMULTANEOUSLY, so a chain that is only payable SEQUENTIALLY --
// an untap ritual refloating the colours of already-tapped sources mid-chain (Reality Spasm), a
// coloured ritual burst funding a later cast, a same-subset Hinata resolving before her discounted
// payload -- reads as unpayable and the line is never offered (gi=22: the one-phase T4 win chain
// Hinata+Spasm+Ponder+Crackle dies at the flat gate in every X variant). When a gate is ABOUT to
// reject an INTERACTING subset, this flag re-tests it with a real per-cast sequential payment on a
// scratch state (the executor's own TapForCostDirect + ApplyRitualFloat + live cost recompute, in
// CastOrderRank order) and offers the subset iff every cast genuinely pays. RESCUE-ONLY: it can
// only ADD candidates the gates wrongly dropped, never remove one. EnumeratePlans (search branch
// list) only -- NEVER Solve::consider (the rollout leaf; the 2026-07-23 MTG_FEASIBILITY_GATE
// dead-end: anything slow in the rollout wedges the suite), and never the d0 greedy (no rollout to
// validate; the Medallion precedent). See docs/design/enumeration-feasibility-via-executor.md.
inline bool ExecFeasEnabled()
{
    static const bool env_on = EnvOn("MTG_EXEC_FEAS", true);
    return heurarm::Flag(heurarm::EXEC_FEAS, env_on);
}

// MTG_EDF_SEQ_AURA -- the LAND AURA twin of MTG_EDF_SEQ_ETB below, and a THIRD independent
// admission to the sequential walk for the same reason: MTG_EXEC_FEAS is default OFF, so gating a
// land Aura on it would make this dead code at ship settings (the repo's standing
// lever-behind-a-default-off-gate lesson).
//
// A land Aura ("enchanted land taps for an additional {G}") is a same-turn mana PRODUCER, and unlike
// a ritual it makes mana that did not exist -- so a subset like {Wild Growth, Living Wish} is
// payable only SEQUENTIALLY and the flat gate rejects it. USER, 2026-09-05, seed 2 gi=1 turn 2:
// "land=Yavimaya Coast; cast=Wild Growth; cast=Living Wish" and the Eladamri's Call variant, both
// rules-legal, both refused. This deck runs SIXTEEN land auras, so the gap costs a spell on a large
// share of its development turns.
//
// Inert for every other deck: `is_land_aura` is carried only by this deck's four Auras.
inline bool SeqLandAuraEnabled()
{
    static const bool env_on = EnvOn("MTG_EDF_SEQ_AURA", true);
    return heurarm::Flag(heurarm::EDF_SEQ_AURA, env_on);
}

// MTG_EDF_SEQ_ETB -- ADOPTED DEFAULT-ON 2026-09-02; `=0` restores the old behaviour (an ETB-untap
// chain the flat pool rejects is dropped). Evidence, paired on (seed, gi) over 800 game-pairs at
// play settings (d5/20ms), negative = better: -0.0338 avg win turns, se 0.0085, t -3.97, 8/8 seeds
// better, 26 games faster : 4 slower : 770 identical -- and 0.937x the COST, because the lines it
// unlocks end games sooner and a shorter game is less search. Suites: 0 configs changed.
//
// It admits "when this creature enters, untap up to N lands" chains to the rescue
// walk above. It is an INDEPENDENT admission, not a sub-clause of MTG_EXEC_FEAS, and that is
// deliberate: MTG_EXEC_FEAS is default OFF, so gating this on it made it dead code at ship settings
// (measured -- a 3-arm scout put MTG_EXEC_FEAS alone at a byte-identical digest to baseline on the
// only deck that has these cards). With MTG_EXEC_FEAS off, the only subsets that reach the walk are
// ones holding an etb_untap_lands cast, so every other deck stays byte-identical.
//
// An ETB untap is a same-turn mana interaction exactly like a ritual's float -- it just arrives
// AFTER its own cast resolves rather than before. That ordering is the whole point: crediting the
// refund into the FLAT pool was unsound (the Stage 5d sweep caught a Peregrine Drake paying for
// itself), whereas SubsetPayableSequential pays each cast in CastOrderRank order against a real
// GameState and fires the untap between casts, so it answers "is this chain payable IN ORDER"
// instead of "is the total big enough".
inline bool SeqEtbUntapEnabled()
{
    static const bool env_on = EnvOn("MTG_EDF_SEQ_ETB", true);
    return heurarm::Flag(heurarm::EDF_SEQ_ETB, env_on);
}

// MTG_EDF_AURA_RANGE -- part 2 of the USER-approved EldraziDisplacerFlicker cast-order package
// (2026-10-14; docs/design/edf-cast-order-2026-10-14.md). DEFAULT ON; =0 restores the fixed land-Aura
// slots byte-identically.
//
// THE RANGE. Fertile Ground, Trace of Abundance and Overgrowth cost more than the mana they add this
// turn (mana value > land_aura_extra_mana), so cast at their slot -- ahead of the untappers -- they
// can leave an untapper unpayable: five lands, {Fertile Ground, Peregrine Drake} pays only Drake-first
// (the Drake taps ahead, refunds five lands, then the Aura), and the fixed order walked Fertile Ground
// first and refused the pair every time. The USER's ruling (Q30): "cast first as now, and moved
// behind the untappers only when the turn is otherwise unpayable". Wild Growth costs nothing net and
// keeps its slot.
//
// WHERE IT LIVES: EldraziFlickerProvider::ArrangeCastOrder, the one set-level hook every site that
// realises or projects a cast order calls (both apply worlds, the enumeration's sequential walk, the
// canonical report) -- so executor and rollout stay in lockstep by construction. The projection is
// the enumeration's own sequential walk (TurnSolver::OrderPaysSequentially: each cast paid on a
// scratch board with the tap-ahead, the ETB untap and the Aura's bonus resolved between casts), after
// the whole-turn prepay the apply worlds try first. Read by the provider and by Solve's rescue gate
// (TurnSolver.cpp), hence one shared reader. Inert for every deck without an etb_untap_lands card in
// the same plan as a land Aura -- EldraziDisplacerFlicker is the only list that holds one.
inline bool EdfAuraRangeOn()
{
    static const bool env_on = EnvOn("MTG_EDF_AURA_RANGE", true);   // DEFAULT ON; =0 disables
    return heurarm::Flag(heurarm::EDF_AURA_RANGE, env_on);
}
// MTG_EDF_AURA_RANGE_SOLVE (part 2b; DEFAULT ON, inert unless MTG_EDF_AURA_RANGE is on): Solve's rescue of
// a subset the range arranges (the d0 runner and the horizon playouts). =0 keeps the range in the search
// window and the two applies only -- the split the A/B uses to attribute part 2 by where it acts.
inline bool EdfAuraRangeSolveOn()
{
    static const bool env_on = EnvOn("MTG_EDF_AURA_RANGE_SOLVE", true);   // DEFAULT ON; =0 disables
    return EdfAuraRangeOn() && heurarm::Flag(heurarm::EDF_AURA_RANGE_SOLVE, env_on);
}
// MTG_EDF_AURA_RANGE_HOSTS (part 2c; DEFAULT ON, inert unless MTG_EDF_AURA_RANGE is on). The plan dedup
// folds land-Aura HOST variants into one class (MTG_EDF_AURA_HOST_SIG_KAROO: the host is in the signature
// only on the karoo-drop branch) and keeps the class's FIRST member. That fold assumes the variants are
// copies of one line, and on a plan that casts an untapper they are not: the range makes a host variant
// payable by MOVING its Aura behind the untapper, and the untap refreshes whichever host carries the bonus.
// Seed 11311458 gi 1457, T3: {Overgrowth, Cloud of Faeries, Eldrazi Displacer} with the Overgrowth on the
// fresh Yavimaya Coast kills (Overgrowth, then Cloud off the Coast's {U}{G}{G}, the Displacer with three to
// spare); on Conservatory it pays only Cloud-first and strands the first blink. The range made the
// Conservatory variant payable, it sorted first and won the class, and the kill was unfindable at --depth 8
// --budget-ms 0. 2c's two tiers: a ONE-Aura plan with an untapper keeps every host variant (the provider
// proposes two hosts, so at most two members); a plan with two or more host-declared Auras keeps its
// first rank-order member and its first MOVED member. Armed only when the candidates hold an untapper
// (only EDF). Additive: it only ever keeps a plan the fold would have dropped. NOT covered: seed 11310005
// gi 4, whose T4 kill needs a host assignment inside the go-off continuation's five-plus-Aura plans --
// keeping every host of every class recovers it, but 2^k variants drove the pooled batch's resident set
// from ~4.5 GB to the 35 GB cap, and a ceiling of up to four Auras does not recover it.
// See docs/design/edf-cast-order-2026-10-14.md.
inline bool EdfAuraRangeHostsOn()
{
    static const bool env_on = EnvOn("MTG_EDF_AURA_RANGE_HOSTS", true);   // DEFAULT ON; =0 disables
    return EdfAuraRangeOn() && heurarm::Flag(heurarm::EDF_AURA_RANGE_HOSTS, env_on);
}

// MTG_IRENCRAG_WASTE -- ADOPTED DEFAULT-ON 2026-09-01 (USER: "let's adopt the Irencrag gate");
// `=0` restores the old behaviour. Adoption evidence, paired 1200x2 at play settings (d5/20ms),
// negative = better: SHIPPED engine hold -0.0167 (t -3.26, 20 better : 4 worse) / train -0.0117
// (t -2.75, 16:4). It also closes most of the greedy-deletion gap: the greedy-free arm goes from
// +0.0175/+0.0383 vs shipped to +0.0033 (t 0.32, indistinguishable) / +0.0233. The lethal
// exemption was verified FREE (identical results, zero games moved -- no dropped plan was ever
// lethal), and the Opus/Soulfire concern was measured out: of 587,676 gate drops over 300 games,
// ZERO hold either payoff.
//
// WHAT IT DOES: reject a subset that casts a cast-
// RESTRICTING ritual (Irencrag Feat, max_casts_after) with NOTHING ranked after it. The restrictor
// exists to fund the spell that follows; with no follower the plan spends a card and {1}{R}{R}{R}
// to float seven red nothing can consume, and spends the turn's one remaining cast doing it.
// USER's order ruling makes this exact: Irencrag is SECOND LAST (rank 22) and Crackle (23) is the
// only cast that may follow, so `after == 0` means no consumer exists. Found as the top case in
// the greedy-deletion continuation diff (MTG_CONT_DIFF): 20,117 of 55,006 differences are the
// canonical continuation casting Irencrag where the greedy continuation correctly declines it,
// which is a large part of why deleting the greedy costs quality on this deck today.
// HEURISTIC, not legality: a post-breakpoint continuation could still spend the float off a drawn
// payoff, so this can delete a real (rare) line -- hence a lever to be measured, not a rule.
// Read by BOTH the enumerator's subset gate and its Solve::consider twin -- lockstep.
// MTG_IRENCRAG_FINISHER -- STRICTER variant of the gate (default OFF): require the follower to be
// the FINISHER (Crackle), not merely some spell. This is the USER's doctrine exactly ("it can only
// be cast before Crackle"; with a Crackle, Irencrag is worth 5+ damage, and Irencrag -> Soulfire /
// -> Opus are bad value because neither gets the opponent into lethal range without Hinata or
// Crackle). MEASURED WORSE than the loose gate anyway: strict vs loose +0.0150 hold (t 2.41) /
// +0.0075 train, and it introduces a new loss class (14-15 worse games where loose had 4).
// Kept as a lever so the doctrine-vs-measurement gap stays testable -- see the Crackle-timing
// hypothesis (USER: "the only way this could cause a bit of an issue is by playing the Crackle for
// damage early, but with search we should be able to find the 'hold it' route").
// Which follower justifies casting the restrictor. 0 = ANY cast, 1 = a PAYOFF (Crackle / Soulfire
// / Opus) i.e. no cantrips, 2 = the FINISHER only (Crackle), 3 = ADOPTED DEFAULT: a payoff that
// COULD NOT HAVE BEEN CAST WITHOUT THE FLOAT.
//
// Rule 3 is the one that states the actual principle, and the others are kept because the route to
// it was measured, not guessed (searched-design-deck-rollout.md 6e). Rule 2 is the USER's stated
// doctrine -- "it can only be cast before Crackle" -- and it measured WORSE at every budget rung
// (20/80/320 ms), which root-caused to a real play it forbids: Soulfire Eruption is {6}{R}{R}{R} =
// NINE mana and exiles+stages a card per target, so on turn 3 off ~5 mana Irencrag is the only way
// to cast the deck's DIG (hold gi=664: the loose arm casts it, digs into two lands, and wins T5;
// the doctrine arm cannot, draws two dead counterspells, and never wins). With Hinata out Soulfire
// costs 3-4, the pool covers it, and spending Irencrag on it IS the waste the doctrine describes.
// Rule 3 separates those two cases by asking whether the float was NEEDED rather than which card
// followed. USER 2026-09-01: "we'll go with your rule, but just those 3 cards."
// Measured 1200x2 vs the loose rule: hold -0.0008, train -0.0017 (6 better : 2 worse) -- quality-
// neutral, and it deletes a class of plays nobody defends (the restrictor spent on a cantrip).
inline int IrencragRule()
{
    // heurarm slots (not an env int) so ONE pooled batch can carry all three arms -- the manifest
    // can only override boolean slots, and CLAUDE.md forbids splitting a sweep into per-arm runs.
    static const bool env_pay = EnvOn("MTG_IRENCRAG_PAYOFF");
    static const bool env_fin = EnvOn("MTG_IRENCRAG_FINISHER");
    static const bool env_ned = EnvOn("MTG_IRENCRAG_NEEDS", true);   // ADOPTED DEFAULT (rule 3)
    if (heurarm::Flag(heurarm::IRENCRAG_NEEDS,    env_ned)) { return 3; }
    if (heurarm::Flag(heurarm::IRENCRAG_FINISHER, env_fin)) { return 2; }
    if (heurarm::Flag(heurarm::IRENCRAG_PAYOFF,   env_pay)) { return 1; }
    return 0;
}

inline bool IrencragWasteGateEnabled()
{
    static const bool env_on = EnvOn("MTG_IRENCRAG_WASTE", true);   // DEFAULT ON; =0 reverts
    return heurarm::Flag(heurarm::IRENCRAG_WASTE, env_on);
}

// MTG_DORK_GROWTH -- same-turn SCALED-MANA-DORK growth (Priest of Titania / Elvish Archdruid):
// a dork whose one-tap yield is the live count of its subtype grows with every matching creature
// cast this turn, so (a) EnumeratePlans credits a subset's matching casts into each live dork's
// burst, (b) such creatures cast in their own early tier (CastOrderRank 7, cheapest first) and
// (c) scaled dorks tap LAST among sources (ManaSourceRank 61) -- the executor half that realises
// the credit (USER 2026-08-20: "play every elf we can [without] tapping scaling dorks or Wirewood
// Lodge. Then ... every elf remaining with scaled mana"). Read by the search (TurnSolver credit),
// the shared cast-order comparator (ManaPayment) and the provider ranks (DecisionProviders), all
// of which both the executor and the rollout consult -- shared reader per the lockstep rule.
// Param-gated on a live scaled dork at every site -> byte-identical for every deck without one.
inline bool DorkGrowthEnabled()
{
    static const bool v = EnvOn("MTG_DORK_GROWTH", true);   // DEFAULT ON; =0 restores the un-modelled gap
    return v;
}
// MTG_UNTAP_BURST_GROWTH (DEFAULT ON; =0 reverts): the same-plan scaled-dork growth credit also
// counts each live untap-burst land (Wirewood Lodge) once per feeder, because the burst re-taps the
// GROWN dork, and the {X} sizing is widened by that growth (DorkGrowthXPotential). USER 2026-10-06,
// SelesnyaLifegain s4_gi3 T6: Genesis Wave capped at X=8 on a board that pays X=10.
inline bool UntapBurstGrowthEnabled()
{
    static const bool v = EnvOn("MTG_UNTAP_BURST_GROWTH", true);
    return v;
}

// MTG_HOLD_RELEASE_YIELD -- the whole-turn prepay's creature HOLD LADDER (TurnSolver::
// BatchPrepayMainCasts: the one payer the executor and the rollout both run, so it stays in lockstep
// by construction) becomes battlefield-position-free and prices what a body's tap is worth as mana:
//   (a) the partial hold releases equal-power bodies BIGGEST YIELD FIRST -- one tap of Priest of
//       Titania pays what three 1/1 elves would, and each costs the attack one body -- instead of
//       lowest battlefield index first;
//   (b) the single-attacker rung holds, among the strongest attackers that are mana sources, the one
//       whose tap is worth LEAST as mana (a 1/1 Llanowar rather than an equal-power Priest);
//   (c) a creature the line SACRIFICES as a cost (Natural Order's victim, the sac-fodder pin) is never
//       held: it cannot attack after its own cast (CR 601.2h pays the cost during casting);
//   (d) the single-attacker rung (hold ONE body) is tried AFTER the partial rungs (hold as many as the
//       turn spares), not before them -- before them it paid gi287's 6-mana turn holding one Fyndhorn
//       and tapped the other ahead of Priest whenever Priest sat later on the battlefield.
// Remaining ties go to card name, then card number -- never the battlefield index. A summoning-sick
// dork can neither tap for mana nor attack (CR 302.6), so it is outside the hold set as before.
// Found on StompySurprise gi1526 (docs/design/stompy-order-2026-10-14.md §4): the T3 kill (Archdruid +
// Natural Order -> Craterhoof, 21) was found iff Priest of Titania was NOT on the battlefield after both
// 1-drop elves (24 of 24 permutations), and the USER cast order always lands the 1-drops first.
// Payment is the permitted greedy area; this only re-keys its tie-breaks. DEFAULT OFF until measured;
// =1 enables.
// DEFAULT ON at integration 2026-10-15 (v6: clears the bar; pending the USER); =0 disables.
inline bool HoldReleaseYieldEnabled()
{
    static const bool v = EnvOn("MTG_HOLD_RELEASE_YIELD", true);
    return heurarm::Flag(heurarm::HOLD_RELEASE_YIELD, v);
}

// MTG_HOLD_ATK_FALLBACK (live only under MTG_HOLD_RELEASE_YIELD; DEFAULT ON, =0 restores the single
// pick byte-identically) -- (b)'s single-attacker rung holds the strongest mana attacker whose tap is
// worth least as mana, and nothing else of that power was ever tried: when holding that ONE body makes
// the turn unpayable (it is a colour the bill needs), the ladder fell to the unrestricted solve, which
// may tap the very attacker a second equal-power body could have been held as. Under this lever every
// other untapped mana attacker of the same power gets its own single-attacker rung, in (b)'s order
// (yield, then name, then number), after the pick's rung -- so a payable pick is unchanged and the
// fallbacks are tried only after it fails. SelesnyaLifegain overnight s7088 gi81 T5: (b) held the
// Accomplished Alchemist (the only {W} besides Blossoming Sands, for Nykthos Paragon's {W}{W}); the
// unrestricted solve then tapped Elvish Archdruid, the 12/12 attacker the T5 kill needed (T6 at every
// depth and budget -- payment is the permitted greedy area, not a searched axis). Holding Archdruid
// pays the turn exactly. Payment only, one shared payer -> executor/rollout lockstep.
inline bool HoldAtkFallbackEnabled()
{
    static const bool v = EnvOn("MTG_HOLD_ATK_FALLBACK", true);
    return heurarm::Flag(heurarm::HOLD_ATK_FALLBACK, v);
}

// MTG_HOLD_PRODUCER_FIRST=1 (live only under MTG_HOLD_RELEASE_YIELD) -- two more rules for the same hold
// ladder (TurnSolver::BatchPrepayMainCasts):
//   (e) when the ladder must release held bodies, the BIGGEST PRODUCER releases first (yield, then power,
//       then name / number): under a team pump every body is one attacker, and one tap of a 4-Elf Elvish
//       Archdruid pays what four 1/1 elves would, each of which costs the attack a body. (a) only ordered
//       EQUAL-power bodies by yield, so a power-2 Archdruid outlived every 1/1 on the ladder;
//   (f) a body ANY of the turn's casts sacrifices as a cost is never held -- every Natural Order victim,
//       not only the first one (c) covers; a second pin of the same copy takes another copy of its name
//       (MTG_SAC_VICTIM_SAME_NAME's reading), lowest card number.
// Found on stompy2hg d3 gi2290 (docs/design/stompy-order-2026-10-14.md §4d): T4 Natural Order x2 ->
// Craterhoof x2 is exactly 8 mana with Archdruid tapped while four Elves live and one 1/1 held to attack;
// every rung held Archdruid, the prepay declined, and the sequential payer's battlefield order decided the
// kill (scn_2hg2290_t4 / _t4b: same board, two layouts, T4 vs T5 on either arm). Payment is the permitted
// greedy area. DEFAULT OFF until measured; =1 enables.
// DEFAULT ON at integration 2026-10-15 (v6: clears the bar; pending the USER); =0 disables.
inline bool HoldProducerFirstEnabled()
{
    static const bool v = EnvOn("MTG_HOLD_PRODUCER_FIRST", true);
    return heurarm::Flag(heurarm::HOLD_PRODUCER_FIRST, v);
}

// MTG_SAC_VICTIM_SAME_NAME=1 -- a "sacrifice a <color> creature" additional cost (Natural Order) is
// searched by victim NAME: TurnSolver emits one cast variant per distinct name, pinning that name's
// most-expendable copy on the PRE-PLAN board by card number (same-name victims are fungible). A second
// Natural Order in the same plan therefore pins the copy the first one already sacrificed. With the
// lever that stale pin means what the axis means -- ANOTHER copy of that name: ResolveSacVictimId
// (SpellEffects.h) re-pins it to the remaining same-name creature (tapped first: its mana is already
// spent; then lowest card number -- never the battlefield index), and both the payment's sac-victim pin
// and the sacrifice read the re-pinned id, in the executor and the rollout alike (one helper). Without
// it the stale pin fell to the most-expendable creature by power and then BATTLEFIELD POSITION.
// Found on stompy2hg gi160 (docs/design/stompy-order-2026-10-14.md §4b): Natural Order -> Ghalta (+ the
// Craterhoof in hand) and Natural Order -> Craterhoof, sacrificing both tapped Priests and attacking
// with the held Fyndhorn Elves, is 31 against 30; with Fyndhorn entered BETWEEN the two Priests (the
// USER order casts the 1-drop first) the second Natural Order ate Fyndhorn and the kill was
// unfindable at any depth. Rules: any green creature is a legal victim (CR 601.2h pays the cost while
// casting); this only names which. DEFAULT OFF until measured; =1 enables.
// DEFAULT ON at integration 2026-10-15 (v6: clears the bar; pending the USER); =0 disables.
inline bool SacVictimSameNameEnabled()
{
    static const bool v = EnvOn("MTG_SAC_VICTIM_SAME_NAME", true);
    return heurarm::Flag(heurarm::SAC_VICTIM_SAME_NAME, v);
}

// MTG_EXEC_PUT_DEFERRED=1 -- executor lockstep for the put-in-hand breakpoint (MTG_BP_PUT_IN_HAND,
// site 10). The rollout arms it at the END of apply_one as a DEFERRED re-solve (deferred_cantrip_resolve:
// cast the rest of the plan, then run the continuation), at the main level always. The executor's clean
// cast loop matches that (no inline breakpoint; the end-of-main catch-all replays the committed segment),
// but its searched-order and opaque loops treated put_in_hand_armed() as an INLINE site and replayed the
// committed segment at the cast -- before the plan's remaining casts, which is not the line the rollout
// scored. With the lever those two loops defer it exactly like acq_deferred (commit: the catch-all
// replays it; no commit: re-solve once after the main casts). Searched decisions only (lookahead depth
// > 0), as acq_deferred. Found on stompy2hg gi797 (docs/design/stompy-order-2026-10-14.md §4c): T4 Elvish
// Mystic, Vaultborn Tyrant, Natural Order + [Fyndhorn, Fyndhorn, Craterhoof] -- the order's searched
// candidate replayed the three right after Vaultborn's draw, cast the one Fyndhorn in hand and stranded
// the Fyndhorn and Craterhoof that Natural Order then drew. Executor-only (the rollout is already
// deferred), so one reader. DEFAULT OFF until measured; =1 enables.
// DEFAULT ON at integration 2026-10-15 (v6: clears the bar; pending the USER); =0 disables.
inline bool ExecPutDeferredEnabled()
{
    static const bool v = EnvOn("MTG_EXEC_PUT_DEFERRED", true);
    return heurarm::Flag(heurarm::EXEC_PUT_DEFERRED, v);
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
// ADOPTED DEFAULT ON (USER 2026-08-19/20: "it should be in our search order somewhere");
// =0 reverts. The bare position move first measured searched-inert / d0 1-in-1000 worse
// (gi922: the copy's payment competing ahead of the casts -- the parked whole-turn-allocation
// class). The FULL doctrine round (reserved-X Braingeyser + Braingeyser/Regrowth acquisition
// re-solves, this flag; haste-aware tap emission + the Terror stub are unconditional rules
// fixes) measures GREEN: d0 -0.0030 (5 faster / 2 slower, gi922's class remains), searched
// keys byte-identical. GT rebaselined (fivecolour, 3 modes).
inline bool GarthOrderedEnabled()
{
    static const bool v = EnvOn("MTG_GARTH_ORDERED", true);   // DEFAULT ON; =0 reverts
    return v;
}

// MTG_DORK_ATK_SEARCH -- searched dork attack/hold (USER design 2026-08-21: "a full-turn plan
// or a lookahead to decide whether attacking with dorks is correct"; "limit it as much as
// possible with heuristics and search the rest"). The collapsed-main mana hold
// (HoldManaSourceForCollapsedMain) is a greedy answer to a question greedy cannot decide --
// its six exception clauses are per-game patches and it still forfeits winning swings
// (gi113: held the lone exalted Hierarch for a Remedy the line never cast). Under this flag,
// where the hold's verdict is CONTESTED -- a held dork whose released swing would actually
// deal damage (effective power >= 1 incl. lone-exalted; 0-power dorks stay greedily held, the
// obvious case and most dorks) -- the search evaluates BOTH combat variants and the committed
// line carries the choice (Plan::atk_dork_release -> AIEngine pin, discard-pin pattern).
// Heuristics close every obvious case (0-power hold, vigilance attack, no-m2-need attack via
// the hold's own trigger), so the branch fires rarely and costs one extra combat+tail only
// there. Read by the search branch site (TurnSolver FSLineWin), the hold (DecisionProviders),
// and the executor pin (AIEngine) -- shared reader per the lockstep rule. DEFAULT OFF pending
// measurement + user review.
inline bool DorkAtkSearchEnabled()
{
    // ADOPTED DEFAULT ON 2026-08-22 (USER). Overnight held-out: -22.98 turns on FiveColour and
    // BYTE-IDENTICAL on every other deck -- a single-deck, zero-collateral gain. MTG_DORK_ATK_SEARCH=0
    // reverts. (Designed for Anti-Lifegain's gi852; the deck that banks it is FiveColour.)
    static const bool v = EnvOn("MTG_DORK_ATK_SEARCH", true);
    return v;
}

// Per-thread combat-variant override for the searched dork attack/hold. -1 = natural (the
// heuristic hold decides); 1 = RELEASE (held dorks attack). Set ONLY (a) by the FSLineWin
// branch around the alternate SimulateCombat, and (b) by the executor's DeclareAttackers when
// the committed line pinned a release -- never ambient, so every other combat is byte-identical.
inline thread_local int g_dork_atk_override = -1;

// HOLD DIRECTION of the searched dork attack/hold (default ON inside MTG_DORK_ATK_SEARCH; =0
// reverts to the release-only branch for an A/B). Without it DorkAtkContested is one-directional:
// it can turn a greedy HOLD into an ATTACK but never an ATTACK into a HOLD, because a dork the
// greedy wants to swing is counted as a natural attacker and never contested. USER 2026-08-21:
// "we should contest the dork when main 2 has a use for the mana." Origin: AL gi852 -- the lone
// Hierarch swings for 1 (lone exalted) and taps the 6th of exactly 6 sources, so the deferred main
// affords ONE Fiery Justice (10 damage) instead of two (20 = exactly lethal). Trading 1 for 10.
inline bool DorkAtkHoldDirEnabled()
{
    static const bool v = EnvOn("MTG_DORK_ATK_HOLD_DIR", true);
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

// MTG_SITE9_REOPEN (DEFAULT ON; =0 restores "site 9 opens once per apply") -- AccomplishedAlchemist
// chunk E1. After a site-9 (post-entry activation) continuation has been APPLIED, a further ability it
// made activatable -- the drain of a Scholar of Athreos the continuation cast, Ezuri's team pump, a loop
// funded for a NEW need -- re-opens the same site as the next breakpoint occurrence, keyed against the
// activation snapshot taken just before that continuation (the delta rule, unchanged). Each re-open is
// an ordinary counted occurrence (bp_seen / bp_seen_exec): applied only for a plan whose choice targets
// it (bp_all or bp_at == idx), searched-only exactly like the first, so a plan without a variant for it
// realises the line as scored. Read by ApplyPlanDirect AND the executor's site-9 twin -- shared reader.
inline bool Site9ReopenEnabled()
{
    static const bool v = EnvOn("MTG_SITE9_REOPEN", true);   // DEFAULT ON; =0 disables
    return v;
}
// SAFETY ONLY (the kUntapLoopCeiling shape): a re-open needs an applied, non-empty continuation that
// made a NEW ability activatable, so a real chain is a handful deep; this only stops a mis-modelled
// ability that re-keys itself forever. Never a lever.
constexpr int kSite9ReopenCeiling = 32;

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

// MTG_BP_ETB_DIG -- DEFAULT ON (USER ruling 2026-09-26, the Pirates D6 review: "my understanding is
// that a breakpoint should open which would allow us to cast the new spell ... If that doesn't
// happen, I agree it is a bug."). =0 restores the pre-fix world for the A/B.
//
// THE DEFECT. An ETB library dig (etb_dig_count: Staunch Crewmate, Acclaimed Contender) puts a card
// in hand at resolution, and the general put-in-hand rule (site 10, MTG_BP_PUT_IN_HAND) is exactly
// the breakpoint that should let the rest of the turn cast it. But TurnSolver::ParamKeyedDrawClass
// listed etb_dig_count -- "some param-keyed site already owns this cast" -- so site 10 stood down in
// BOTH worlds, while the only param-keyed dig site (ApplyPlanDirect's PerformEtbDig call) had its
// arming deliberately removed (the 2026-08-19 MTG_ACQ_DIG measurement). Claimed, never armed: the
// dug card could not be cast in the phase that dug it at any searched budget. Pirates s777011 gi11:
// T3 Crewmate -> dig Daring Buccaneer -> cast it off the leftover {R} wins T4; the search could only
// win T5, at --budget-ms 200 and 20000 alike.
//
// WHY THE 2026-08-19 REJECTION DOES NOT STAND. It measured a re-arm feeding the then-GREEDY
// continuation (6/8 held-out searched keys red, attributed to that greedy misplaying). The
// continuation is a SEARCHED node now, and the USER's general rule (2026-09-18, see
// BpPutInHandEnabled) is that opening the breakpoint is a correctness requirement, not a lever.
//
// THE FIX IS A DELETION FROM THE CLAIM LIST, not a new arming site: with etb_dig_count out of
// ParamKeyedDrawClass, site 10's existing outcome-keyed arming (rollout: end of apply_one; executor:
// put_in_hand_armed) fires after the dig in lockstep -- both read the same function and the same
// deterministic dig -- and PlanOpensBreakpoint's site-10 route list names etb_dig_count so the
// continuation is fanned, not the unchallengeable canon default. CAST path only: a VIAL-put digger
// is outside any cast window, which is the MTG_BP_HAND_ENTRY hole (default OFF, measuring).
inline bool BpEtbDigEnabled()
{
    static const bool v = EnvOn("MTG_BP_ETB_DIG", true);   // DEFAULT ON; =0 reverts
    return heurarm::Flag(heurarm::BP_ETB_DIG, v);
}

// MTG_BP_RECORD_VIAL -- DEFAULT ON (2026-09-26); =0 restores the unrecorded put for the A/B.
// A LOCKSTEP fix, not a heuristic: an Aether Vial put applied inside a breakpoint CONTINUATION was
// never written to the committed breakpoint script (apply_vial had no sink_stack record, unlike the
// cast / SacForMana / Suspend branches), so the executor's replay_recorded never deployed it -- the
// rollout scored a creature the real turn never had. Latent until MTG_BP_ETB_DIG opened a breakpoint
// after Acclaimed Contender's dig, whose continuation typically has no mana left and so can only
// Vial the dug card in; the phantom then biased Knights toward Vial / Contender lines (smoke gi114,
// gi215, gi88, knights2hg gi29, every one an MTG_FD_ORACLE fd-diverge, predicted T4 realised T5).
inline bool BpRecordVialEnabled()
{
    static const bool v = EnvOn("MTG_BP_RECORD_VIAL", true);   // DEFAULT ON; =0 reverts
    return v;
}

// MTG_TOP_RESOLVE=1 -- measurement lever (DEFAULT OFF until the adoption A/B is accepted): the
// USER's tutor-to-TOP reset (StompySurprise cast-order review, 2026-08-21: "We need to build the
// reset for my combo to be workable ... Cast worldly tutor -> now activations and Turntimber can
// be cast"). A tutor_to_top spell (Worldly Tutor) is a LIBRARY WRITE, not a draw: its resolution
// re-arms every top-of-library consumer -- a Call of the Wild activation or a Turntimber cast
// fired after it takes the KNOWN stacked creature instead of gambling on an unknown top ("they
// will almost certainly be better if Worldly Tutor was just cast"). So the cast arms the same
// deferred post-cast re-solve as the ACQ family: the continuation enumerates on the post-tutor
// state, where Turntimber's candidate collection sees the stacked card and activations re-score
// against it. Consumers stay independently castable on their own ("we should be able to cast
// Turntimber or consider activations on their own") -- the reset only ADDS the post-tutor round.
// With enough mana the search can loop it across copies (tutor {G} + activation {2}{G}{G} per
// fatty). Read by the rollout apply (TurnSolver arming) and the executor's draw-engine
// classification (AIEngine note_draw_engine + is_draw_engine) -- shared reader, lockstep pair.
// EVERY site reads it through TopResolveActive(state) (DecisionProviders.h) = this global lever OR
// the deck's opt-in (DecisionProvider::TopResolveOptIn): StompySurprise ships the reset with its USER
// order (MTG_STOMPY_TOP_RESOLVE, 2026-10-14) while this global default stays OFF for every other deck.
inline bool TopResolveEnabled()
{
    // Per-job overridable (heurarm) so the ORDER and the ORDER+LOOP arms of the StompySurprise
    // adoption A/B run in ONE pooled batch. The order's rank-15 Turntimber / rank-19 Natural Order
    // placements were designed ASSUMING this reset exists (the USER's proposal opens with it:
    // "we could put the search cards above and trigger something like a breakpoint that re-enables
    // cards that interact with the top of the library"), so measuring the order without it measures
    // half a design. Unset everywhere => the env default, byte-identical.
    static const bool v = EnvOn("MTG_TOP_RESOLVE");   // default OFF; =1 enables (A/B lever)
    return heurarm::Flag(heurarm::TOP_RESOLVE, v);
}

// MTG_BP_PUT_IN_HAND=1 -- THE GENERAL RULE (USER 2026-09-18): *"Coatl should open a breakpoint just
// like Astrolabe and every other card that puts things in hand. That might need an update, since
// this is a general rule."* / *"We shouldn't need to fiddle around with individual cards for this.
// It should be automatic."*
//
// WHAT IS WRONG TODAY. Both worlds decide "did this cast put a card in hand?" from a WHITELIST of
// params and templates that has grown one clause at a time -- AIEngine's note_draw_engine and
// is_draw_engine, and TurnSolver's seven arming sites: DrawUntilNonland, cascade_max_mv,
// shuffle_reveal_freecast, etb_exile_until_nonland, stages_cards, impulse_exile, solo_target_trick
// with a draw/Treasure payload, tutor_to_hand, tutor_to_top, etb_dig_count, EquipmentDrawBreakpoint.
// `etb_self_draw` is in NONE of them, so a permanent whose ETB draws -- Ice-Fang Coatl AND Arcum's
// Astrolabe, the whole family -- arms nothing. The card enters, a card appears in hand, and the turn
// continues as if it had not: the drawn card cannot be cast in the phase that drew it.
//
// The whitelist is the defect, not its contents. Every clause above was added because some card fell
// through, and the next one will too.
//
// THE RULE IS THE OBSERVATION, NOT A PREDICATE ON THE CARD: a cast arms a breakpoint iff the caster's
// hand actually GAINED A CARD when it resolved. Card-agnostic, param-agnostic, and it cannot be
// out of date -- a card implemented tomorrow is covered the day it is implemented.
//
// CONTENT-ANCHORED, never a size: a resolution that draws one card and discards another leaves the
// hand the same SIZE, and the cast card itself has left hand, so only "a number present now that was
// not present before" is the honest test (TurnSolver::HandGainedACard). Both worlds already capture
// the pre-cast snapshot this needs -- ApplyPlanDirect's `hand_at_cast` and AIEngine's `rdb_hand` --
// so the rule reads state both sides already agree on, which is what keeps bp_at numbering in
// LOCKSTEP. That lockstep is why this is one shared reader and not two flags.
//
// DEFAULT ON (USER 2026-09-18: *"We do need to open the breakpoints regardless. Then the idea is to
// see whether condemnation can help at all."*).
//
// THE COST IS NOT AN ARGUMENT AGAINST OPENING THEM, and that ordering is deliberate. This is the same
// ruling site 8 already carries -- *"same-turn playability of the found card is a correctness
// requirement (USER 2026-09-06, 'we need to be able to play it'), not a search lever"*. A card the
// turn drew that the turn cannot then cast is a line the engine simply cannot express, at any budget
// or depth; leaving the class shut because it is cheaper is the "narrow the rule until it is free"
// move this arc has already had to undo twice (the exclusive-slot guard sparing 81% of drops; the
// activation rule's inverted gate). Open the class, THEN ask condemnation to pay for it.
//
// =0 is the hatch, and it is what every pre-2026-09-18 measurement in this file was taken under.
inline bool BpPutInHandEnabled()
{
    static const bool v = EnvOn("MTG_BP_PUT_IN_HAND", true);   // DEFAULT ON; =0 reverts
    return v;
}

// MTG_BP_HAND_ENTRY -- THE REST OF THE GENERAL RULE. DEFAULT ON since 2026-09-27 (USER: "we should
// turn it on and evaluate"); =0 reverts. Adopted together with MTG_BP_NODE_S10 (the node hosts site
// 10, so a base plan's continuation there is an explicit EMPTY child plus the enumerated list rather
// than an unchallengeable canon default) -- the pair that was measured.
//
// WHAT SITE 10 ALREADY DOES, and what it cannot. BpPutInHandEnabled asks the outcome instead of the
// card -- but it asks it in ONE window: bracketing a CAST (`hand_at_cast` in the rollout,
// `rdb_hand` in the executor). A card that enters hand outside a cast's apply arms nothing, at any
// depth or budget, silently. So the rule is general over CARDS and still a taxonomy over EVENTS.
//
// THE HOLE IS MEASURED, not argued (MTG_HAND_ENTRY_CENSUS, 40 games/deck d3 b10, seed 1001;
// rollout-inclusive counts, so read the shares, not the absolutes). Entries of NEW MATERIAL that
// happen OUTSIDE any cast apply, excluding the Karoo land-bounce route (a land returning after the
// land drop is spent is not a new castable option):
//
//     fungus   draw   43,055 of 43,055  = 100%   <- the deck's ONLY draw is Psychotrope Thallid's
//                                                   "{1}, Sacrifice a Saproling: Draw a card" --
//                                                   an ACTIVATED ability, so it has never once
//                                                   opened a breakpoint on any turn
//     goblins  stage   7,401 of  7,401  = 100%   <- a death trigger's impulse exile
//     goblins  tutor   2,384 of  3,257  =  73%   <- Matron's ETB off a Lackey/Muxus PUT: the
//                                                   design doc's named live instance, confirmed
//     melira   tutor  68,478 of 86,272  =  79%   <- the pod chain's ETB tutors (pod is ACTIVATED)
//     melira   draw   13,442 of 19,793  =  68%
//     knights  dig     4,096 of 21,566  =  19%
//     kitty    draw    4,316 of 154,030 =   2.8%
//     th       reveal  1,726 of 670,263 =   0.26%
//     hinata   all       ~312 of ~889k  =   0.03%
//     mirrorwing draw     150 of 311,861=   0.05%
//
// So this is NOT an everything fix -- the cantrip decks are already covered by site 10 -- and the
// design doc's own question ("a Goblins fix or an everything fix?") resolves to: a Goblins-class
// fix, whose largest case is the deck currently being optimised.
//
// HOW IT ARMS, and why this is one placement rather than thirty. The rollout takes ONE section-level
// hand snapshot at the top of ApplyPlanDirect and checks it once, immediately before the deferred
// re-solve loop -- the point every deferred class already resolves at. It fires only when NO class
// armed for the whole section, so it catches exactly the hole and renumbers nothing: it arms the
// EXISTING site 10 (deferred_put_armed), keeping BpSiteMask numbering intact, which the design doc
// calls "the single largest hazard in this change".
//
// EXACT, not the counter. g_hand_entry_seq (core/HandEntry.h) is only a pre-filter: the rollout
// speculates on COPIES of GameState on the same thread and those bump the same counter, so a
// changed sequence means "ask the exact question". The exact question is HandGainedACard, a content
// diff on the live state. See the header for the full argument.
//
// It was DEFAULT OFF while measuring, because arming on every hand entry is STRICTLY MORE armings
// (the design doc's own warning) and a breakpoint that opens is work whether or not it pays. What
// the measurement found (2026-09-27, all in breakpoints-should-key-on-hand-entry.md and
// rollout-executor-lockstep.md #9): the searched losses under the flag were two DEFECTS, not the
// cost -- a continuation's Vial put was never recorded for the replay, and the section window did
// not reset at inline breakpoints (double-arming site 10) -- and with both fixed no searched smoke
// loss survives --budget-ms 0. Cost on the adoption binary (60 games d3 b10): Knights +13.7%
// interior nodes (+18.5% with the node host), Treasure Hunt +0%, avg win turn identical; the smoke
// tier read faster 5 / slower 3 with all three slower games budget churn. What the flag still buys
// once MTG_BP_ETB_DIG covers the CAST-Contender dig is the Vial-put Contender in Knights and, the
// larger case, the activated-ability / trigger / put-ETB entries that are 68-100% of new material in
// Melira, Fungus and Goblins. The asymmetry that decided it is the USER's: a missing arm is
// unreachable at any budget and silent, an unhelpful arm is only cost.
inline bool BpHandEntryEnabled()
{
    static const bool v = EnvOn("MTG_BP_HAND_ENTRY", true);   // DEFAULT ON (2026-09-27); =0 reverts
    return heurarm::Flag(heurarm::BP_HAND_ENTRY, v);
}

// MTG_BP_CAST_SUBTYPE -- the site-10 fan-out route for a CAST-SUBTYPE draw watcher (Sram, Senior
// Edificer: "whenever you cast an Aura, Equipment, or Vehicle spell, draw a card").
//
// DEFAULT ON, because the doctrine the canon audit enforces is not optional: an arming route
// without a fan-out clause leaves the continuation as an unchallengeable cands.front() at ANY
// depth or budget, and the audit's own instruction is to fix a nonzero `unreachable` "by giving
// that site a clause ... never by suppressing the audit".
//
// It is nonetheless a FLAG rather than a bare clause because it is a genuine WIDENING with a real
// cost, and on KittyEquipment v2 that cost is large: the route carried 16,306 of 25,282 canon
// defaults (64.50%) -- the deck's whole engine is "cast a free Equipment, draw, cast another", so
// almost every plan opens it. =0 restores the pre-fix behaviour for a clean A/B.
inline bool BpCastSubtypeEnabled()
{
    static const bool v = EnvOn("MTG_BP_CAST_SUBTYPE", true);   // DEFAULT ON (2026-10-01); =0 reverts
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

// MTG_LAND_IDLE_TAPPED_FIRST -- DEFAULT OFF (measuring). Invert the ranker's untapped-first
// preference on the FIRST land drop of the game when the mana provably cannot be spent this turn.
// The full rationale, the safety conditions and the USER ruling behind it are at the one call site
// (GreedyLandChoiceIndex, LandPlay.cpp); this is the shared reader because the ranker serves all
// three land-drop sites (executor drop, enumeration tiebreak, rollout playout under
// MTG_ROLLOUT_LAND_RANKER).
inline bool LandIdleTappedFirstEnabled()
{
    static const bool v = EnvOn("MTG_LAND_IDLE_TAPPED_FIRST");
    return heurarm::Flag(heurarm::LAND_IDLE_TAPPED_FIRST, v);
}

// MTG_NO_REDUNDANT_REDUCER -- DEFAULT OFF (measuring). Do not offer a cast whose cost-reduction a
// copy already in play has saturated: it spends a card and its mana for no change at all. See
// IsSaturatedCyclingReducer (SpellEffects.h) for the argument and the reference game it costs.
inline bool NoRedundantReducerEnabled()
{
    static const bool v = EnvOn("MTG_NO_REDUNDANT_REDUCER");
    return heurarm::Flag(heurarm::NO_REDUNDANT_REDUCER, v);
}

// MTG_DIG_HOLD_FUEL -- DEFAULT ON; =0 restores the fuel-casting re-solve for the isolating A/B.
// The DIG-SITE half of HoldFuelWhileComboing ("don't play any fuel once you are going off; just
// cycle everything", USER 2026-09-05): while the provider says the chain can close, the dig loop's
// nested re-solve is skipped so the draw becomes a ping instead of a board object. Two sites read
// the same provider rule (the plan-order comparator and this one) and per the two-sites-two-levers
// law each carries its own gate; the rule itself stays inert on every deck whose provider returns
// false, so this default changes nothing outside an opted-in provider.
inline bool DigHoldFuelEnabled()
{
    static const bool v = EnvOn("MTG_DIG_HOLD_FUEL", true);
    return heurarm::Flag(heurarm::DIG_HOLD_FUEL, v);
}

// MTG_GREEDY_HOLD_LAND -- DEFAULT ON; =0 restores the unconditional greedy drop for the A/B.
// The LAND-DROP site of the same rule (its third site, own lever): at depth 0 the drop runs before
// the dig loop, so playing a drawn cycling land removes the turn's only ping from hand. Skipping
// the drop leaves it for the dig loop to cycle. Reads the same provider predicate, whose guards
// (no Stinger deployed / no enabler / library cannot close) are the user's stated exceptions.
inline bool GreedyHoldLandEnabled()
{
    static const bool v = EnvOn("MTG_GREEDY_HOLD_LAND", true);
    return heurarm::Flag(heurarm::GREEDY_HOLD_LAND, v);
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

// MTG_ACT_TAP_RESERVE=1 -- measurement lever (DEFAULT OFF; it changes which source pays, so it moves
// ground truth): a planned `{cost}, {T}` activation's SOURCE is held back from mana payment for the
// whole plan, so no earlier cost in the same plan can spend it and strand the activation.
//
// THE DEFECT, from a replay (snow_smoke_d0_s1001 gi413, turn 4). The plan is "activate Scrying Sheets
// #39, activate Scrying Sheets #38": each look costs {1}{S} and taps its own Sheets, and the board has
// Boreal Druid + Rimewood Falls + Coldsteel Heart + Snow-Covered Island untapped -- exactly the 4 mana
// the two looks need. The trailing pass taps #39 for its {T}, then pays {1}{S} with the FIRST sources
// the greedy finds, which include Sheets #38 (it taps for {C}). #38 is now tapped, so the second
// activation is stranded and the turn gets ONE look instead of two. Nothing was unaffordable; the
// payment simply spent the source the plan's own later action had to tap.
//
// It rides the SAME reserve-then-fallback retry as every other entry in g_plan_reserved_sources (try
// holding them; if the cost cannot be met, pay normally), so it can only ever change WHICH sources
// pay, never whether a cost is payable -- and therefore can never drop an action.
//
// WHY IT IS THE OTHER HALF OF MTG_RESCUE_TAP_SOURCE. That flag stops the ENUMERATOR from offering
// self-funded plans (a source paying for its own activation). Both must move together: with the gate
// alone, the enumerator correctly refuses the illegal 3-activation plan whose PARTIAL execution used
// to deliver a good 2-activation line by accident, and the legal plan that replaces it is then
// stranded by this defect -- which is exactly how a correctness fix measured as three d0 regressions.
// Read by BOTH apply paths through TurnSolver::ActivationTapReserve -> PlanReserveSources (the cast
// section) and the trailing dispatchers' own scope, per the lockstep rule.
inline bool ActTapReserveEnabled()
{
    static const bool env_on = EnvOn("MTG_ACT_TAP_RESERVE");
    return heurarm::Flag(heurarm::ACT_TAP_RESERVE, env_on);
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
// (TurnSolver::Plan::vial_charge_choice). DEFAULT OFF since 3efbe969 (2026-08-30): the shipped
// decider is the hand-aware root heuristic; the fan measured a standing net loss (-108 turns /
// 34,325 suite games), so `=1` OPTS IN (and is further gated by MTG_VIAL_AXIS_NARROW).
// Originally adopted default-ON 2026-08-18, user-directed: "the option to search needs to remain for all of the decks
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
// DEFAULT FLIPPED TO OFF, 2026-08-30 (user): "in general we don't want to fully search vial
// decisions, because that is a waste of effort. However, that doesn't prevent the option from being
// open if it is needed. Though, if we did take it, we would only do so under certain
// circumstances." So the ROOT heuristic (WantVialCharge, hand-aware) ships and the fan is opt-in.
//
// WHY THIS SUPERSEDES THE 2026-08-18 ADOPTION rather than merely disagreeing with it. That commit
// (b289661b) bundled TWO changes: it fixed WantVialCharge, which had been returning flat `false` so
// a Vial in a non-VialProvider deck never gained a counter in its life, AND it added this axis. Its
// held-out -0.1275 is the SUM of both, dominated by goblins -0.2035 -- plausibly the heuristic fix,
// which was enormous. Its own table already showed the axis COSTING knights +0.0120 and slivers
// +0.0640, excused then as "a budget race, not worse judgment".
//
// Isolated on 2026-08-30 with the heuristic already correct, removing the fan is worth -108 turns
// over 34,325 suite games (smoke -6.98/1,125, regression -25.02/4,700, held-out overnight
// -76.00/28,500) and 3.1x less search on an idle-box 4-deck probe. The deck it helps MOST is
// slivers (-62 turns) -- exactly the deck the 2026-08-18 table said it cost. The two measurements
// agree; what is new is that the budget race is a standing net loss, not a wash.
//
// THE OPTION STAYS OPEN, which is the part of the original direction that has NOT changed:
//   MTG_VIAL_AXIS=1                        -> fan, GATED (MTG_VIAL_AXIS_NARROW below): the
//                                             "certain circumstances" form.
//   MTG_VIAL_AXIS=1 MTG_VIAL_AXIS_NARROW=0 -> the exact pre-2026-08-30 unconditional fan, for A/B
//                                             against that era's ground truth.
// Rule 0b still bites the gated form: "hold at k because I will draw an MV-k creature next turn" is
// unreachable under the gate even at infinite budget. That cost is now accepted knowingly instead of
// being disqualifying -- it is only ever paid by someone who has opted INTO the axis.
//
// Read by BOTH the rollout (TurnSolver: variant emission + SimulateBeginningPhase consume) and the
// executor (AIEngine::DecideVialCharge, which retires the probe when the axis owns the decision) --
// shared reader per the lockstep rule.
inline bool VialAxisEnabled()
{
    static const bool v = EnvOn("MTG_VIAL_AXIS", false);
    return v;
}

// MTG_VIAL_AXIS_NARROW -- when the axis IS opted into, restrict the fan to the one call the
// heuristic declines to make. WantVialCharge is deterministic and hand-aware everywhere except the
// tradeoff its own comment defers ("deploy a cheaper creature now vs climb to a lethal bigger one"),
// which is live only when the hand holds a creature ABOVE the deck's vial_target_mv. Default ON:
// fanning a call the heuristic already answers is precisely what the measurement above priced at
// 3.1x for nothing. `=0` restores the unconditional fan. Inert unless MTG_VIAL_AXIS=1.
inline bool VialAxisNarrow()
{
    static const bool v = EnvOn("MTG_VIAL_AXIS_NARROW", true);
    return v;
}

// MTG_EQUIP_PAY_GUARD=1 -- measurement lever (DEFAULT OFF until the adoption A/B is accepted):
// do not PAY an equip cost that ApplyEquip is going to refuse. Both apply paths currently pay
// first and apply second, so a plan whose co-selected host cast was dropped as unpayable still
// taps for the equip and then attaches nothing -- and the executor logs the attach anyway.
// See docs/design/equip-host-not-on-battlefield.md for the measurement and the reproducer.
//
// Read by BOTH the executor (AIEngine's Equip branch) and the rollout (ApplyPlanDirect's) --
// shared reader per the lockstep rule, because fixing one alone would make the search project a
// mana cost the game does not pay. On adoption this flips to default-ON with an off-hatch and a
// GT rebaseline (it changes play wherever it fires: the mana is kept).
inline bool EquipPayGuardEnabled()
{
    static const bool v = EnvOn("MTG_EQUIP_PAY_GUARD");
    return heurarm::Flag(heurarm::EQUIP_PAY_GUARD, v);
}

// MTG_EQUIP_LOG_TRUTH=1 -- the LOG half of the equip defect, separable from the payment half above
// and much cheaper. The executor emits its "equip -> host" ability line on the path that PAID, not
// on the path that ATTACHED, so when ApplyEquip refuses (no host on the battlefield) the game log
// claims an attach the very next board snapshot contradicts. That misleads the play viewer, whose
// whole job is surfacing engine bugs, and any reference JSON saved from it.
//
// Executor-only (the rollout has no logger), and it changes NO play -- only whether a line is
// emitted in the handful of games where the attach did not happen. It is still digest-moving,
// because LogAbility folds into the play digest by design (the same "deliberate fingerprint
// improvement" that made equip destinations visible to the digest in the first place).
inline bool EquipLogTruthEnabled()
{
    static const bool v = EnvOn("MTG_EQUIP_LOG_TRUTH");
    return heurarm::Flag(heurarm::EQUIP_LOG_TRUTH, v);
}

// MTG_SCALED_LAND_RANK=1 -- DEFAULT OFF pending the adoption A/B.
//
// ManaSourceRank reserves every OTHER board-scaled source (scaled dork 61, storage land 62, live
// untap-burst Lodge 63) but has no tier for a board-scaled LAND, because IsScaledManaDork is gated
// on `IsCreature() && feeder == 0` and Three Tree City is a land with a {2} feeder. It therefore
// falls through to the plain colour ladder, and since eaccc120 gave a {C}-only source rank 5
// ("least flexible -> spend FIRST"), the highest-yield source on the board became the first one
// tapped.
//
// Measured on Goblins d0 s4004 gi90 (the eaccc120 bisect's first bad commit). T4, four lands, five
// Goblins out. Three Tree City's scaled mode is `{2},{T}: Add {R} per Goblin` = 5 red for a {2}
// feeder, net +3; its basic mode is one {C}. Paying Aether Vial's {1} with the CITY spends the
// multiplier for one generic pip and leaves three Mountains -> ONE Siege-Gang activation, opponent
// survives at 1. Paying it with a Mountain leaves the city up -> {2} in, five {R} out -> TWO
// activations, exact lethal, win T4. Pre-eaccc120 the rank tie put a Mountain first and the game
// was won on T4; this is the regression that tie removal introduced.
//
// The gate is ScaledManaNetYield() > 0 -- the engine's own "the scaled mode is live, affordable,
// and beats the basic {C} tap" predicate -- exactly mirroring how tier 63 gates on
// UntapLandBurstNet() > 0. A source whose scaled mode is dead or unaffordable is untouched and
// keeps its plain {C} rank, so no deck without a live scaled land can move.
inline bool ScaledLandRankEnabled()
{
    static const bool v = EnvOn("MTG_SCALED_LAND_RANK");
    return heurarm::Flag(heurarm::SCALED_LAND_RANK, v);
}

// MTG_FRONTLINE_FIRST -- DEFAULT ON; =0 takes the CONSERVATIVE trigger order.
//
// Frontline Heroism's copy trigger and a Zada/Mirrorwing magnet trigger both fire on the same
// cast, and CR 603.3b lets the controller stack them in either order. The two orders differ:
//   ON  (default): Heroism resolves FIRST, so its Soldier is already on the battlefield when the
//                  magnet trigger reads "each other creature you control that the spell could
//                  target" -- the Soldier gets the magnet's copy too. Measured 5 payload
//                  instances on a Zada + 2-creature board.
//   OFF          : the magnet resolves first and never sees the Soldier -- 4 instances.
// ON is the goldfish-optimal order and the one a real controller would take, so it ships as the
// default. OFF exists to BOUND how much of Heroism's measured value depends on that choice
// rather than on the card, which is a question a screen cannot answer from the delta alone.
// Inert for every board with no frontline_copy_tokens permanent.
inline bool FrontlineTriggerFirst()
{
    static const bool v = EnvOn("MTG_FRONTLINE_FIRST", true);
    return v;
}

// ---- MTG_MINT_CREDIT_EXACT: a minted Treasure is CREDITED, never BREAKPOINTED ------------------
//
// THE GENERAL RULE (USER, 2026-09-22), stated as the test every breakpoint site has to pass:
//
//   A breakpoint exists for a card that ARRIVES -- something the plan could not enumerate at the
//   base because it was not in the original hand (a draw, a dig, a reveal, a tutor). Mana a plan's
//   own action PRODUCES is not an arrival: it is priced at the base like every other in-plan
//   source (a ritual's float, a rock's tap, a fixed-colour sac, a hasted dork), and the plan that
//   spends it is a BASE plan. "We make the credit work for everything else in the plan that
//   produces mana, so following that rule we should count creation of treasures." A line that
//   becomes payable only because a DRAWN card produces mana is a NEW line by definition -- it
//   casts or uses something that was not in the original hand -- and the new-only continuation
//   rule keeps it on that ground. "We should not need to reconsider things from the original hand."
//   "Otherwise we need to randomly open breakpoints on treasure creation and worry about lines
//   that we already deleted."
//
// What this lever changes (DEFAULT ON since 2026-09-22 -- adopted under the user's quality+speed
// rule on the v21 build: Mirrorwing paired 3,000 at shipped settings -0.043 (84 / 1) 20-life,
// -0.061 (64 / 3) 2HG at 0.92x units; smoke tier 4 keys better / 0 worse / 3 digest-only at
// 0.99x case time, lean == audit; =0 is the A/B hatch; heurarm slot so one pooled batch carries
// every arm):
//   1. EMISSION stamps Action::mint_gain = the EXACT Treasure count a solo-target trick's cast will
//      realise on the current board (SoloTrickInstances: the magnet fan and the Frontline Heroism
//      copies, per target -- the count ResolveSoloTargetTrick produces), where the shipped credit
//      counted ONE per minting cast and its comment called the width "target-dependent". The
//      target is on the Action, so the width is a board fact.
//   2. BOTH mana gates at the odometer (ManaPruneBound, the selection-exact ManaGateIndex) credit
//      that gain. Neither credited a mint at all before, so the shipped consider() credit was DEAD
//      for exactly the total-mana shortfall it was written for: {Gold Rush {1}{G}, Fists {1}{R}}
//      = 4 against a 3-mana pool is skipped at the odometer and never priced (the mw68 "still
//      OPEN" mechanism in mana-order-and-reserve-overhaul.md -- "the position has to survive to be
//      priced", the metalcraft lesson, a third time).
//   3. The pricing twins (Solve / EnumeratePlans consider()) credit mint_gain, and their
//      spendability gate becomes the payer's own (PaySacSpendableNow's entered_this_turn branch:
//      magnet live, OR Heroism live under MTG_HEROISM_FRESH_HOLD -- the Heroism clause was missing
//      from the credit, so under a Heroism alone the mint was credited at zero while the payer
//      accepted it). The colour-presence gate treats a credited mint as every colour (it is wild).
//   4. An {X} trick (Luxurious Libation) is also offered at the X the same-plan mint would fund,
//      so "Gold Rush, then Libation for the fan" is a base plan and not a continuation.
//   5. The fresh-spend axis is priced at the base: a magnetless subset payable ONLY with its own
//      mint is admitted TAGGED freshmode_choice=1 (the released-hold world FSLineWin already
//      validates by this-turn-lethal), instead of relying on a post-mint re-solve.
//   6. A Treasure-only trick payload opens NO breakpoint (MintPayloadOpensBreakpoint, read at the
//      rollout arming, the plan's site mask, the executor's d0 second pass and its node-hosted
//      twin), and the new-only filter's "needs a minted Treasure" keep -- which reconsidered old-hand
//      cards -- is off.
//
// THE AUDIT ROUTE IS DELIBERATE AND STAYS. USER: "I see a purpose to having a framework to locate
// bugs like this one with the extra treasure token. We do not want to hide these cases, so we
// should have a way to run things with extra breakpoints and reconsiders in order to ensure there
// are no bugs with the full line version." / "Because the full-line version is a bit bug-prone I
// don't want to rely on it fully in isolation." MTG_BP_MINT_SITE=1 re-opens the Treasure-only
// breakpoint under this lever, and MTG_BP_NEW_ONLY_DRY=1 keeps every reconsideration in the list;
// a pooled batch with a lean arm and an audit arm, read with test/paired_arms.py --list-moved, is
// the detector: a game the audit arm wins earlier at unbounded budget is a line the base could not
// enumerate. See docs/design/bp-new-only-continuations.md ("Audit route").
inline bool MintCreditExactOn()
{
    static const bool env_on = EnvOn("MTG_MINT_CREDIT_EXACT", true);
    return heurarm::Flag(heurarm::MINT_CREDIT_EXACT, env_on);
}
inline bool BpMintSiteOn()
{
    static const bool env_on = EnvOn("MTG_BP_MINT_SITE");
    return heurarm::Flag(heurarm::BP_MINT_SITE, env_on);
}
// ONE predicate for every site that asks "does a Treasure-only trick payload open the deferred
// site-5 breakpoint" -- rollout arming, plan site mask, executor d0 pass, executor node twin.
// A draw payload (cast_draw > 0) opens it regardless: a drawn card is an arrival.
inline bool MintPayloadOpensBreakpoint()
{
    return !MintCreditExactOn() || BpMintSiteOn();
}

// ---- MTG_BP_REPLAY_COST: a recorded continuation cast carries the cost it paid ----------------
//
// LOCKSTEP DEFECT (found 2026-09-22 on mirrorwing seed 700473 T4, MTG_BP_TRACE + [bp-traits]):
// the rollout records a breakpoint continuation's casts into plan.breakpoint_actions as bare
// Actions (name, target, X, ...) with NO `cost`. The executor replays them under PlanTraits
// computed from those records (replay_recorded's _rec_traits, the lockstep twin of the rollout's
// _cont_traits), and ComputePlanTraits counts a mana cast only when a.cost.ManaValue() > 0 -- so
// the replay's traits read mana_casts=0 where the rollout's read 3. That flips the per-payment
// one-shot hold (OneShotHoldMask fires on mana_casts < 2): the executor held the turn-old Treasure
// and tapped BOTH dorks for two Fortifying Draughts; the rollout had cracked the Treasure and kept
// the pumped Elvish Mystic to attack alone for exactly lethal. Committed T4 kill, realised no win.
// Every committed continuation with >= 2 mana casts and an untapped pay-sac source pays in two
// different worlds. The fix stamps the paid cost (the apply's effective cost) onto the record,
// which only the traits builder reads (replay re-derives the cost from the card). DEFAULT ON
// since 2026-09-22 (adopted with MTG_MINT_CREDIT_EXACT; byte-identical on every smoke key on its
// own, antilife included); =0 is the A/B hatch; heurarm slot for the pooled batch.
inline bool BpReplayCostOn()
{
    static const bool env_on = EnvOn("MTG_BP_REPLAY_COST", true);
    return heurarm::Flag(heurarm::BP_REPLAY_COST, env_on);
}

// MTG_BP_RESUME_LINE_HOLD -- DEFAULT ON; =0 restores the phantom hold byte-identically (A/B hatch,
// heurarm slot BP_RESUME_LINE_HOLD for the pooled batch).
//
// A resumed apply must leave exactly the state a full apply would (BpPrefixSnap's own contract). The
// snapshot carried the board, the sink and the scripted pins, but NOT the line's unpaid-cost hold
// (g_line_unpaid_cost): ApplyPlanDirect binds it from the WHOLE plan's bill at entry and apply_one
// subtracts each cast as it pays, so at the deferred re-solve a full apply holds only what the plan
// still owes, while a resume -- which skips the prefix casts -- held the full bill, including casts
// already paid. The payer reads that hold (LineFloatTapFirstPlan, the line-aware float spend, the
// replicate / sink holds), so every node child and every prefix-cache wave variant paid its
// continuation under a phantom demand the executor never has. The executor never resumes; it plays
// the committed line through a full apply, so the scored line and the played line split on payment.
//
// Hinata s12111487 gi 486 (MTG_HINATA_GAMBLE_PAST_DORK=1, --depth 8 --budget-ms 0): the T5 main-2
// node's child [Reality Spasm, Reality Spasm, Preordain] -> Ponder -> [Sol Ring, Ornithopter,
// Irencrag Feat + Remand] -> [Irencrag Feat, Crackle] ran under Soulfire Eruption's already-paid
// {R}{R}{R}, so Ornithopter's {2} was paid with Sol Ring and the floating {R} kept -- one red more
// for the recast Irencrag. The game (line = 0 there) paid the {2} with the floating {R} + wild, the
// recast Irencrag was one {R} short, and Crackle stayed in hand: committed T5, realised T6. Read by
// TurnSolver only (the resume branch of ApplyPlanDirect); kept here with the other lockstep levers.
inline bool BpResumeLineHoldOn()
{
    static const bool env_on = EnvOn("MTG_BP_RESUME_LINE_HOLD", true);   // DEFAULT ON; =0 disables
    return heurarm::Flag(heurarm::BP_RESUME_LINE_HOLD, env_on);
}

// MTG_EXEC_CLEAN_BP_DEFERRED -- DEFAULT ON; =0 restores the inline replay byte-identically (heurarm slot
// EXEC_CLEAN_BP_DEFERRED). The executor's CLEAN-set cast loop replayed a committed breakpoint segment AT THE
// CAST for every class equip_bp_truncates answers for, but only site 6's INLINE class is resolved at the cast
// by the rollout: a plain cantrip's partition under MTG_BP_NODE (and sites 5 / 6-deferred) is DEFER +
// TRUNCATE in ApplyPlanDirect -- the continuation runs after the main casts and the trailing passes,
// including the deferred Karoo drop. A plain cantrip reaches the clean set only through a REMAND rider
// (BpNameOf is the Remand; the cast itself, e.g. Soulfire Eruption, carries no opaque param), which is why
// the "site 6 is the only class in a clean set" premise held until Remand was modelled. Under the flag a
// committed deferred-class segment truncates the plan there, as the opaque loops do, and the end-of-main
// catch-all replays it after the Karoo -- the rollout's point. Hinata s12111487 gi 486
// (MTG_HINATA_GAMBLE_PAST_DORK=1, d5 b30): T5 m1 Soulfire + Remand -> [Reality Spasm] ran BEFORE the
// Boilerworks, whose bounce then returned the Island the Spasm had just untapped, and the Boilerworks stayed
// tapped; the rollout played the Karoo first (bouncing the spent Island) and its Spasm untapped the
// Boilerworks -- two more mana for main 2 and the T5 kill the search had scored. Executor only (the rollout is
// the reference); a non-committed plan keeps its inline re-solve.
inline bool ExecCleanBpDeferredOn()
{
    static const bool env_on = EnvOn("MTG_EXEC_CLEAN_BP_DEFERRED", true);   // DEFAULT ON; =0 disables
    return heurarm::Flag(heurarm::EXEC_CLEAN_BP_DEFERRED, env_on);
}



// ---- UNBUDGETED-PLAY SCOPE + the leaf memo it USED to arm -------------------------------------
//
// MTG_UNBUDGETED_LEAF_MEMO -- DEFAULT ON; =0 kill switch. (Historical name.)
//
// 2026-10-15: NO LONGER LATCHED. The user ruled out unbudgeted-only changes ("I don't like changes that
// only impact unbudgeted ... the only exception is where the change structurally requires unbudgeted"),
// and the argument below is about keeping budgeted UNITS still, not about soundness: the memo stores a
// bound-qualified no-win only from a run that nothing CUT (TurnSolver.cpp, SimulateToEnd's store guard:
// no truncation booked, no budget bind beneath it -- budgetbind in SearchBudget.h -- and no decision-
// work exit), so a hit returns the budget-free answer at any budget (MTG_NOWIN_VERIFY recomputes it
// unbudgeted).
// NoWinLeafMemoArmed() now reads only the flags; the latch below still scopes the dedup skips that use
// UnbudgetedWorkScopeActive() (each needs its own truncation review before it can follow). The text
// that follows is the original rationale, kept for the record.
//
// WHAT IT ARMS. The leaf transposition table (SimulateToEnd's) memoizes only WINS: a no-win
// result is discarded because it may be a branch-and-bound abort rather than a genuine no-win.
// On a deck whose rollouts overwhelmingly do NOT win inside the horizon that throws away the
// commonest result in the search, and every leaf re-rolls from scratch -- the same asymmetry
// FSLineCache shed in 2026-08-05 and TTNoWinCacheOn has been parked on ("DEFAULT OFF pending
// measurement") ever since. This flag turns the BOUND-QUALIFIED no-win half on, but ONLY where
// it cannot touch a measured number: when the play search is genuinely unbudgeted.
//
// WHY THE GATE IS STRUCTURAL, NOT MERELY MEASURED. A memo hit skips a rollout, and skipping a
// rollout skips its ConsumeAt() calls -- so the deterministic work-unit count MOVES, and with it
// every budget-derived decision (the iterative-deepening start gate, the overrun guard, the
// abandon ceiling) and the regression fingerprint that budgeted play feeds. Budgeted play must
// therefore be UNREACHABLE, not "measured unchanged": the latch below can only be raised by a
// frame that holds a play budget of 0 virtual ms, so with any real budget the whole mechanism is
// dead code on that thread.
//
// WHERE THE LATCH IS RAISED. AIEngine::TakeTurn -- the one frame that owns the REAL play budget
// (m_budget_ms). It is deliberately NOT `budget->Unlimited()` at the leaf: several sub-budgets
// inside a BUDGETED search are default-constructed and therefore Unlimited (the escalation's
// probe_cap_budget / esc_alloc_budget / meas_budget), so that test would arm inside budgeted play
// -- exactly the failure the structural gate exists to prevent. It is also NOT
// WinlessCertificateActive's `g_unbounded_label_search` arm: the label path
// (EnumerateEarliestWins) is under separate active work and neither its cost nor its answers may
// move underneath it.
inline thread_local int g_unbudgeted_play = 0;

// Scoped raise. Save/restore rather than a bare set: mulligan/bottoming re-enter the engine with
// a temporarily different budget (BottomEvalScope), so the latch has to unwind with the frame.
struct UnbudgetedPlayScope
{
    int saved;
    explicit UnbudgetedPlayScope(bool unbudgeted)
        : saved(g_unbudgeted_play) { g_unbudgeted_play = unbudgeted ? 1 : 0; }
    ~UnbudgetedPlayScope() { g_unbudgeted_play = saved; }
};

inline bool UnbudgetedLeafMemoOn()
{
    static const bool v = EnvOn("MTG_UNBUDGETED_LEAF_MEMO", true);
    return v;
}

// MTG_PLAY_SEGMENT_ALWAYS (DEFAULT ON; =0 restores the old draw-only re-prompt). Human play
// re-enters the main-phase chooser after EVERY committed line, so "Commit Line literally means let
// me play more things" (USER 2026-09-04) and only an explicit pass ends the phase.
//
// Shared reader per the lockstep rule, because there are now TWO readers and they must agree:
//   * the executor -- AIEngine's external-chooser segment loop, which does the re-prompting;
//   * the enumerator -- TurnSolver's saturated subset collapse, whose ENTIRE soundness argument is
//     that a dropped combined plan {A,B} is still reachable as "commit A, then commit B". With the
//     segment loop off, committing A ends the phase and {A,B} would be genuinely unreachable, so
//     the collapse must not fire. One reader means the collapse cannot be left armed against a
//     segment loop somebody turned off.
inline bool PlaySegmentAlwaysEnabled()
{
    static const bool v = EnvOn("MTG_PLAY_SEGMENT_ALWAYS", true);
    return v;
}

// MTG_HUMAN_PRE_TAP (DEFAULT ON; =0 removes the fallback entirely). The viewer's MANUAL TAP/PAY
// fallback: `tap=<name>#<num>:<COLOR>` tokens let a human tap specific sources for specific faces
// into the float before a committed line's payments run, so a poorly-allocated engine tap can be
// corrected by hand (USER, EldraziDisplacerFlicker 2026-09). See docs/design/viewer-manual-tap-pay.md.
//
// Shared reader per the lockstep rule -- there are FOUR readers and a disagreement between any two
// is a line that validates one way and executes another:
//   * TurnSolver::CheckLine       -- performs the taps on its state copy before the affordability walk;
//   * TurnSolver::ApplyPlanDirect -- performs them for real at their declared positions;
//   * AIEngine::ReorderPlanCasts  -- lifts them out of the --cast-order full-order list;
//   * main.cpp's WriteDecisionJson -- publishes the per-source `taps` affordance the GUI offers from.
// OFF everywhere but human play regardless (every site also tests HumanPlayActive()), so rollouts,
// autonomous play and GT are byte-identical by construction.
inline bool HumanPreTapEnabled()
{
    static const bool v = EnvOn("MTG_HUMAN_PRE_TAP", true);
    return v;
}

// QUEUED-CONTINUATION UNTAP DEMAND (docs/design/viewer-line-macros.md). DEFAULT ON; =0 disables.
// Shared reader per the lockstep rule -- there are TWO readers and a disagreement between them
// means a declared demand is lifted out of the order list but never acted on (or vice versa):
//   * AIEngine::ReorderPlanCasts -- lifts `need=` out of the --cast-order full-order list;
//   * SpellEffects.h's EtbUntapLands -- diverts one untap pick toward the declared colours.
// Both sites also test HumanPlayActive(), and the search never writes Plan::human_untap_need, so
// rollouts, autonomous play and GT are byte-identical by construction.
inline bool HumanUntapDemandEnabled()
{
    static const bool v = EnvOn("MTG_UNTAP_LINE_DEMAND", true);
    return v;
}

// ---- VIEWER STEP TIMING (MTG_PLAY_STEP_TIMING; DIAGNOSTIC, DEFAULT OFF) ----------------------
//
// Where does one play-viewer click GO? The stateless protocol re-simulates the whole --choices
// prefix, so a click's cost is the SUM over every already-decided frame of (enumerate + apply) --
// and that sum is invisible from outside: /usr/bin/time gives one number for the whole replay and
// perf does not work in this container (no hardware counters, and `perf record` fails to write).
// Without a split, every attempt to attribute the cost is guesswork, which is how "the combo-off
// trial is the expensive part" came to be assumed rather than measured (it is not, on the frames
// measured 2026-09-11 -- the base enumeration is).
//
// Shared reader + shared accumulator per the lockstep rule: the enumerator half lives in
// TurnSolver::EnumerateMainPlans and the apply half in AIEngine's external-chooser segment loop,
// and a split that measured only one of them would mis-attribute the other's cost to it.
//
// Off (the default) every Scope is one predictable branch and no clock read. Printed once at
// process exit, to stderr, so it never contaminates the decision JSON the viewer parses.
//
// SINGLE-THREADED BY ASSUMPTION, stated rather than enforced: it is for a --claude-play viewer
// replay, which is one game on one thread. The accumulator is a plain struct, so setting this on a
// pooled `--batch` run would race the counters. That would spoil the numbers, nothing else -- the
// flag changes no behaviour -- but do not read a batch's line as a measurement.
namespace playtiming
{
inline bool On()
{
    static const bool v = EnvOn("MTG_PLAY_STEP_TIMING");
    return v;
}

struct Totals
{
    double    enum_total = 0;   // all of EnumerateMainPlans
    double    enum_base  = 0;   //   ... of which EnumeratePlansWithLand (the fan itself)
    double    co_rules   = 0;   //   ... of which the provider's ComboOffPossible rule table
    double    co_project = 0;   //   ... of which the cheap lethal projection
    double    co_trial   = 0;   //   ... of which the trial ApplyPlanDirect verifies
    double    apply      = 0;   // TurnSolver::ApplyPlan on the committed plan
    long long frames     = 0;   // main-phase frames enumerated
    long long trials     = 0;   // trial applies run
};

inline Totals& T()
{
    static Totals t;
    return t;
}

// Accumulate into `sink` for the lifetime of the scope. Nested scopes are fine: each bucket is
// reported on its own, and the "of which" buckets are subsets of enum_total by construction.
struct Scope
{
    double*                               sink;
    bool                                  on;
    std::chrono::steady_clock::time_point t0;
    explicit Scope(double* s) : sink(s), on(On())
    {
        if (on) { t0 = std::chrono::steady_clock::now(); }
    }
    ~Scope()
    {
        if (!on) { return; }
        *sink += std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - t0).count();
    }
    Scope(const Scope&)            = delete;
    Scope& operator=(const Scope&) = delete;
};

// Printed from a static destructor, which std::exit(70) -- the protocol's "more input needed"
// exit -- still runs. Same idiom as enummemo::Dumper.
struct Dumper
{
    ~Dumper()
    {
        if (!On()) { return; }
        const Totals& t     = T();
        const double  other = t.enum_total - t.enum_base - t.co_rules - t.co_project - t.co_trial;
        std::fprintf(stderr,
                     "[play-timing] frames=%lld trials=%lld | enum=%.1fms (base=%.1f rules=%.1f "
                     "project=%.1f trial=%.1f other=%.1f) apply=%.1fms\n",
                     t.frames, t.trials, t.enum_total, t.enum_base, t.co_rules, t.co_project,
                     t.co_trial, other, t.apply);
    }
};
inline Dumper g_dumper;
}   // namespace playtiming

// ---- VIEWER PLAN-SPACE VALVE (MTG_VIEWER_PLAN_CAP; HUMAN PLAY ONLY, DEFAULT ON) ---------------
//
// THE BUG IT FIXES: a play-viewer click that never comes back. EDF seed 51 / gi 50, turn 6, in the
// Displacer/Emiel blink loop -- every committed segment mints another Clue token and leaves more
// mana floating, and the main-phase plan odometer multiplies one digit per Clue on top of four
// Eldrazi Displacer digits of 6-7 blink targets each. Measured over ten consecutive clicks of that
// one turn: odometer bound 1.15e5 -> 4.61e5, plans materialised 24,695 -> 330,357, one click
// 0.6 s -> 11.3 s of CPU and still doubling when it was killed. Three such replays burned 80-100
// minutes at ~95% CPU inside ONE decision (docs/design/combo-off-replay-hunt.md §7 HANG-1..3).
// A human on that board does not get a slow viewer, they get a frozen one.
//
// WHY NOTHING STOPPED IT. The engine already owns the right guard: CapGroupsBySituationalRank's
// MTG_PLAN_SPACE_CAP (262,144 positions), added 2026-09-06 for exactly this class on Melira. But
// `--claude-play` does `EnvPut("MTG_UNPRUNED", "1")` for the whole session (src/main.cpp), and that
// makes DecisionUnpruned(UnprunedGate::GroupCap) true -- so the cap returns immediately and the
// VIEWER is the one mode in the engine running with no plan-space bound at all. Un-pruning the
// viewer is right (the human, not a heuristic, owns the decision); un-bounding it is not.
//
// WHAT THIS DOES. Human play only, and only when the odometer product exceeds `Positions()`:
// below the bound the valve returns before touching anything, so every ordinary frame -- and every
// saved reference replay -- enumerates exactly what it does today. Above it, the SAME ranked shrink
// the autonomous cap uses drops the lowest SituationalCardRank groups until the product fits, and
// the drop is RECORDED (Last()) so the decision JSON can say the menu is truncated and the viewer's
// history can tell the player. A cut the player is told about is a usable viewer; a hang is not.
//
// Autonomous play, rollouts and GT are untouched by construction: HumanPlayActive() is false with
// MTG_HUMAN_PLAY unset and inside every HumanPlaySuppress scope, and the valve only ever runs on
// the branch the unprune gate had already turned OFF.
//
// MTG_VIEWER_PLAN_CAP=0 restores the unbounded viewer (the one-binary A/B and the escape hatch);
// MTG_VIEWER_PLAN_CAP_POSITIONS=<n> retunes the bound. Default 65536 positions, chosen from the
// measurement above: the frames at ~25k plans cost ~0.6 s per click, the ones past 100k cost 3-11 s.
namespace viewerplancap
{
inline bool On()
{
    static const bool v = EnvOn("MTG_VIEWER_PLAN_CAP", true);
    return v;
}

// ---- RUNTIME PIN: ONE frame's menu forced to CARRY THE PLAYER'S LINE --------------------------
//
// USER, 2026-10-03: *"If possible I would like to change things in a way that allows these lines to
// be played, but to record that the search failed rather than preventing me from proceeding. It is
// quite the nuisance otherwise."*
//
// WHY AN OVERRIDE AND NOT A BIGGER BOUND. The valve must drop something whenever a frame exceeds the
// bound, and WHATEVER it drops blocks some rules-legal line. Measured on the user's own s15/gi14/t5
// frame: 393,216 payable positions against a 65,536 bound, and the group it dropped was the SECOND
// hand copy of Golem-Skin Gauntlets -- so "cast two Gauntlets this turn" became unreachable while
// every distinct card stayed castable. That is not a bug in the choice of victim; the cover rule
// (78ff03aa) deliberately guarantees one group per distinct CARD, and a second copy of one card is
// outside that guarantee by construction. Raising the bound only moves which line is unreachable,
// which is why it has been refuted three times on this deck.
//
// WHY A PIN AND NOT A SUSPEND, which is what shipped first and which the user's VERY NEXT frame
// broke. Suspending the valve enumerates the frame IN FULL, and "in full" is not a bounded
// quantity: the same seed-15 game reached a T5 frame the valve priced at 768,000,000 positions,
// and materialising that took the process to 17.78 GB, where the RSS cap aborted it. The cap did
// its job and the box survived, but the player's click did not -- and an override that can take
// the process down is not an override, it is a second failure mode. A bound a button can switch
// off is not a bound.
//
// So the valve STAYS ON at its ordinary 65,536 and the override changes WHICH groups it keeps: the
// cards the player's line names are offered the budget FIRST, before the bundle, the per-class
// cover or the rank-fill can spend any of it, and every other pass is bounded exactly as before.
// Memory and time are therefore bounded by the same cap as an ordinary frame, while the one line
// the player asked for is in the menu.
//
// IT ALSO ADDRESSES THE ORIGINAL FRAME MORE DIRECTLY THAN THE SUSPEND DID, which is the part worth
// keeping in mind: the group dropped there was a card's SECOND hand copy, exactly the class the
// cover rule cannot protect (it guarantees one group per distinct CARD). A pin is per GROUP and
// matches by card NAME, so every group carrying a named card survives -- second copies included.
//
// SCOPED, NOT A FLAG, because determinism is load-bearing: the viewer shows a menu in one process
// and commits an INDEX into it, so both must enumerate the same list. The chooser re-enumerates
// under this scope and REPLACES the menu it was handed, keyed on the main ordinal, so the live
// frame and every later replay of the recorded ordinal produce the same list. (The index-drift bug
// this protects against was reported as "it put the Trace of Abundance on the Conservatory when I
// specifically put it on the Aether Hub".)
inline std::vector<std::string>& PinnedNames()
{
    static thread_local std::vector<std::string> v;
    return v;
}
inline bool AnyPinned() { return !PinnedNames().empty(); }
inline bool IsPinned(const std::string& name)
{
    for (const std::string& n : PinnedNames()) { if (n == name) { return true; } }
    return false;
}
struct PinScope
{
    explicit PinScope(const std::vector<std::string>& names) : m_prev(PinnedNames())
    { PinnedNames() = names; }
    ~PinScope() { PinnedNames() = m_prev; }
    PinScope(const PinScope&)            = delete;
    PinScope& operator=(const PinScope&) = delete;

  private:
    std::vector<std::string> m_prev;
};

// Positions the odometer may walk per enumeration. <= 0 means UNBOUNDED, the same convention
// MTG_PLAN_SPACE_CAP uses -- so either flag alone turns the valve off and neither can surprise
// someone who reached for the one they remembered.
inline double Positions()
{
    static const double v = []() -> double {
        const char* e = std::getenv("MTG_VIEWER_PLAN_CAP_POSITIONS");
        if (e == nullptr || *e == '\0') { return 65536.0; }
        const double d = std::strtod(e, nullptr);
        return d > 0.0 ? d : 0.0;
    }();
    return v;
}

// One frame's truncation record. SINGLE-THREADED BY ASSUMPTION, stated rather than enforced, and
// the assumption is the same one playtiming above makes: this is only ever written under
// HumanPlayActive(), which is one viewer game on one thread.
struct Trunc
{
    int    dropped_groups = 0;   // groups the valve removed (max over the per-land inner calls)
    // Groups removed by POOLING interchangeable duplicates instead of dropping a card's choices --
    // counted separately because the two narrowings are not the same promise to the player. Dropping
    // a group removes an ACTION from the menu (the defect the user hit: "I can't equip various
    // equipment"); pooling a class removes only the redundant Nth copy of an action whose 1st copy
    // is still offered, so every action remains reachable. The history line says which happened.
    int    pooled_groups  = 0;
    // Groups the PLAYER's "play it anyway" override kept by name (see PinnedNames). Counted
    // separately from the two narrowings above because it is the opposite operation -- it is
    // reported so a played-anyway line carries the fact that the ordinary menu did NOT contain it.
    int    pinned_groups  = 0;
    double full_positions = 0;   // odometer product before the shrink
    double kept_positions = 0;   //   ... and after
    // The card names of the groups that were DROPPED (comma-joined, capped). A COUNT alone is not
    // diagnosable: "10 group(s) were DROPPED" cost a round trip with the user to establish that the
    // missing ones were the shields, which is the whole content of the report. Names make the
    // history line answer "what can I no longer do?" on its own.
    std::string dropped_names;
    // The menu actually offered is the ENGINE'S OWN pruned list (MTG_VIEWER_EXECUTOR_FALLBACK), not the
    // truncated unpruned slice the counts above describe. Set by AIEngine after the swap, so the
    // decision JSON stops claiming `dropped_names` are missing from a menu that may well offer them
    // (claude-play sweep seed 80016: "dropped_names: Stroke of Genius" over a menu that cast it).
    bool   executor_menu = false;
    bool   Fired() const { return dropped_groups > 0 || pooled_groups > 0; }
};

// Live accumulator, written by CapGroupsBySituationalRank.
inline Trunc& Acc()
{
    static Trunc t;
    return t;
}

// THE VALVE'S SIZE ESTIMATE (claude-play sweep gi4, 2026-09-27; the full rationale is beside the
// valve in TurnSolver.cpp's CapGroupsBySituationalRank). `group_costs[g]` = the mana values of digit
// g's members. Returns {raw odometer positions, PAYABLE positions}: the knapsack count of selections
// (at most one member per digit) whose summed mana value fits `mana_bound` -- an upper bound on the
// plans the walk materialises, since the enumerator prunes every position over the same all-ramp-
// credited bound. Independents count x2 unconditionally. A bound < 0 or > 256 (a bailed prune, a
// ritual storm) is not usable: payable = raw.
constexpr double kWalkFactor = 64.0;   // the raw walk's own bound = Positions() x this
inline std::pair<double, double> Estimate(const std::vector<std::vector<int>>& group_costs,
                                          int num_independent, int mana_bound)
{
    const double ind = std::ldexp(1.0, std::min(num_independent, 60));
    double raw = ind;
    for (const std::vector<int>& g : group_costs) { raw *= 1.0 + static_cast<double>(g.size()); }
    constexpr int kMaxM = 256;
    if (mana_bound < 0 || mana_bound > kMaxM) { return { raw, raw }; }
    std::vector<double> dp(static_cast<std::size_t>(mana_bound) + 1, 0.0), nx;
    dp[0] = 1.0;
    for (const std::vector<int>& g : group_costs)
    {
        nx = dp;   // the digit's "none" option
        for (int c0 : g)
        {
            const int c = std::max(0, c0);
            for (int m = mana_bound; m >= c; --m) { nx[m] += dp[m - c]; }
        }
        dp.swap(nx);
    }
    double pay = 0.0;
    for (double v : dp) { pay += v; }
    return { raw, pay * ind };
}

// A position COUNT as text, safe for any magnitude. The raw odometer product is a double and on a
// wide board it runs past 1e19, so `static_cast<long long>` is undefined there -- it printed
// `-9223372036854775808` (LLONG_MIN) in a user-visible history line. Integers below 1e15 print
// exactly, because that is the range a player can read; past it, 3 significant figures in
// scientific form, because "7.26e+10 -> 3.27e+09" is what the number is actually for.
inline std::string PositionsText(double v)
{
    if (!(v > 0.0)) { return "0"; }
    char buf[48];
    if (v < 1e15) { std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v)); }
    else          { std::snprintf(buf, sizeof buf, "%.3g", v); }
    return buf;
}

// The record for the frame currently being offered: EnumerateMainPlans clears Acc() before the
// base enumeration and latches it here afterwards, so a combo-off TRIAL apply's own nested
// enumerations cannot overwrite the menu's number.
inline Trunc& Last()
{
    static Trunc t;
    return t;
}
}   // namespace viewerplancap
