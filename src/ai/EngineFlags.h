#pragma once
// Shared readers for env flags that BOTH the executor (AIEngine) and the rollout (TurnSolver)
// must agree on. Each of these was previously a per-TU `static const` copy-pasted into both
// files -- two chances to update one and not the other, which is the executor/rollout lockstep
// failure mode in miniature. One reader per flag; the function-local static means the
// environment is read once per process, same as before.
#include "../core/EnvFlags.h"
#include "HeuristicArm.h"
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
    // Per-job override (see HeuristicArm.h) so ONE pooled batch can carry both arms of the A/B --
    // required to attribute the Hinata all-main-2 arm, which needs this flag, against a control
    // that must not have it. -1 = unset => the env static => byte-identical off the batch path.
    static const bool env_on = EnvOn("MTG_MAIN2_DROP");
    return heurarm::Flag(heurarm::MAIN2_DROP, env_on);
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

// MTG_EXEC_FEAS -- EXECUTOR-VALIDATED sequential subset payability (default OFF -> byte-identical).
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
    static const bool env_on = EnvOn("MTG_EXEC_FEAS");
    return heurarm::Flag(heurarm::EXEC_FEAS, env_on);
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


