#pragma once
// Shared readers for env flags that BOTH the executor (AIEngine) and the rollout (TurnSolver)
// must agree on. Each of these was previously a per-TU `static const` copy-pasted into both
// files -- two chances to update one and not the other, which is the executor/rollout lockstep
// failure mode in miniature. One reader per flag; the function-local static means the
// environment is read once per process, same as before.
#include "../core/EnvFlags.h"
#include "HeuristicArm.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
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

// MTG_BP_HAND_ENTRY=1 -- THE REST OF THE GENERAL RULE (DEFAULT OFF, measuring).
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
// DEFAULT OFF because arming on every hand entry is STRICTLY MORE armings than today (the design
// doc's own warning), and a breakpoint that opens is work whether or not it pays. The asymmetry
// that decides the eventual adoption is the USER's: a missing arm is unreachable at any budget and
// silent, an unhelpful arm is only cost.
inline bool BpHandEntryEnabled()
{
    static const bool v = EnvOn("MTG_BP_HAND_ENTRY");   // DEFAULT OFF (measuring); =1 enables
    return heurarm::Flag(heurarm::BP_HAND_ENTRY, v);
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



// ---- UNBUDGETED-PLAY SCOPE + the leaf memo it arms ------------------------------------------
//
// MTG_UNBUDGETED_LEAF_MEMO -- DEFAULT ON; =0 kill switch.
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
    double full_positions = 0;   // odometer product before the shrink
    double kept_positions = 0;   //   ... and after
    bool   Fired() const { return dropped_groups > 0; }
};

// Live accumulator, written by CapGroupsBySituationalRank.
inline Trunc& Acc()
{
    static Trunc t;
    return t;
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
