# Sacrifice-for-mana ON DEMAND (Lotus Bloom, Lake of the Dead): deferred design

Status: BUILT 2026-11-05 on branch `sb-sacpayer` (see "Implementation (2026-11-05)" at the end); validation
in progress. Was DEFERRED (2026-11-04), queued after the main-phase/cast-order work.

USER 2026-11-04 (verbatim): *"Yes, it should be on-demand and only done that way."* The payer is the ONLY route for
these cracks: retire the Lotus Bloom / Lake of the Dead SacForMana action (step 5 below is required, not optional), and
delete `MTG_IDLE_CRACK_FOLD` (`FoldIdleLandCracks`, default off since 2026-11-04 for its probe cost) together with it. This is the write-up the USER ruling below asked for. The
interim Lake of the Dead fix that shipped is the sound identity fold `MTG_IDLE_CRACK_FOLD`
(`TurnSolver.cpp` `FoldIdleLandCracks`), described in "Lake fold + Profane fetch timing (2026-11-04)" in
`docs/design/analysis-Shadowborn Apostle.md`.

## The ruling

USER 2026-11-04 (verbatim): *"It should be land -> Dark Ritual -> other spells. Lotus bloom should be
sacrificed on demand by the mana system. not a direct activation. Land drop should happen main 1 unless we
searched it up in main 2."*

Earlier the same day, on Lake of the Dead: *"For 2 I agree that it should not be sacrificing the land unless
we need it. We should sort that out if possible."*

Only the second sentence is in scope here. The cast order and the land drop belong to the main-phase
cast-order draft.

## What exists today

* Lotus Bloom's "{T}, Sacrifice: add three mana of any one colour" and Lake of the Dead's
  "{T}, Sacrifice a Swamp: add {B}{B}{B}{B}" are both `Action::Kind::SacForMana` plan actions. Lotus Bloom
  is emitted once per candidate colour (`Action::chosen_float_color`; `TurnSolver::SacFloatColorFor`, with a
  colour-agnostic form at `TurnSolver.h` ~3025). Lake is emitted once per available Swamp victim (folded to one
  by `ChooseSubtypeLandSacrifice`).
* Both apply worlds crack every SacForMana in the plan in a PRE-PASS, before any cast: `ApplyPlanDirect`'s
  `apply_continuation_precasts` (TurnSolver.cpp) and the executor's committed-plan apply (AIEngine.cpp,
  5 sites). The cracks route through `ApplySacForMana` (`SpellEffects.h`).
* The subset walk credits the crack's mana in the walk's mana math (`CreditFixedColorSac`, the Lake's
  +3 delta over its plain tap; the Lotus ritual_float). Then it pays the casts from the resulting float.
* Human play: `HumanStepVerbFor` already treats SacForMana as PAYMENT, not as a step. The viewer's pre-tap
  path has a `Route::SacAction` (ManaPayment.cpp ~9890) that runs the same `ApplySacForMana` when the human
  clicks the source while paying.
* Around 65 `Kind::SacForMana` sites in TurnSolver.cpp: dup-clause filters, the Dragonstorm
  independent-accelerant prefix collapse (~31000), the sac-host rules (~15900), the census, the Niv
  `SacForManaOnlyIfNeeded` hook (~10480) and the rescue walk (~10100).

## What on demand needs

1. **A payer source, not an action.** A sac-for-mana permanent becomes a source the payer may use while it
   pays a cost (`TapForCostDirect` / ManaPayment's source walk): Lotus Bloom yields three of ONE colour, the
   colour picked for that payment (it must stay one colour, never wild). Lake of the Dead's crack is an
   alternative MODE of the same land as its {T}: {B}, so it can never be used alongside the plain tap.
   **Leftover rule:** a crack that pays a cost floats its surplus for the rest of the phase, so later casts
   in the same plan spend that surplus before tapping anything else. Otherwise one crack could be counted
   twice, or a surplus could be wasted while the payer taps more sources.
2. **When to sacrifice: a dominance rule, not a heuristic.** Sacrifice only when the cost cannot be paid
   without it. This is "pay with it only when the plan's cost needs it". Without it the plan's
   untapped-source set would be a superset, so this is sound in goldfish.
   * When several sources are each sufficient, the tie-break is a mana-payment choice, which falls under
     the standing payment exemption in the no-greedy rule.
   * The cost is the colour question. Three of one colour can pay a pip that nothing else can. The payer
     must try the sacrifice before declaring a cost unpayable, and pick the colour from the remaining
     shortfall.
3. **The subset walk's mana math** must count these sources as available capacity. That is what
   `CreditFixedColorSac` does for an action today; it would apply to the source instead. The walk's
   soundness invariant must hold: never reject a subset the executor can pay.
4. **Lockstep.** The executor, the rollout and the human pre-tap must all use the same payer path. The
   on-demand crack has to fire in BOTH apply worlds at the same cast. The decision digest moves for every
   deck with Lotus Bloom (Dragonstorm, Breaching Dragonstorm, Hinata?) or Lake of the Dead (Shadowborn), so
   adopting it is a play change with an A/B and a GT accept.
5. **Retire the action.** Remove the `SacForMana` emission for these two cards from `CollectActions`, along
   with the chosen-colour variants, the Dragonstorm accelerant prefix collapse that keys on SacForMana, the
   dup-clause and sac-host rules, and the pre-pass. Keep `ApplySacForMana` as the shared effect the payer
   calls. Other SacForMana outlets keep the action unless the USER rules otherwise: Generator Servant (whose
   haste rule needs the action, `SERVANT_HASTE_KEEP` / `SacForManaOnlyIfNeeded`), Treasure (already
   `Route::PaySac` in the payer), and Skirk-style repeatable outlets.
6. **Viewer.** The human never sees an action. The source is offered in the pay-click flow, which already
   exists (`Route::SacAction`). Recorded references that contain a SacForMana step must still replay: the
   replay checker maps a recorded crack onto the payment that consumed it. Reference safety rules apply, and
   any reference that breaks is the agent's to fix.
7. **Tests.**
   * Sacrifice only when needed (Lotus Bloom with enough lands: not cracked).
   * The colour is correct (Lotus pays a pip of a colour no land makes).
   * The surplus floats to the next cast.
   * Lake's crack and its plain tap are exclusive.
   * Executor/rollout lockstep digest.
   * Dragonstorm regression rows unchanged or explained.

## Why it was not done in the 2026-11-04 change

It touches the payer, both apply worlds, the subset mana math and a Dragonstorm-specific collapse. It moves
the GT of at least three decks. That is a separate project with its own A/B. The sound fold that shipped
already removes the observed Lake waste: the plans whose crack output is never spent. It stays correct
(and redundant) once the on-demand payer lands. At that point remove it, together with the Lake SacForMana
action.

## Implementation (2026-11-05)

* **Predicate + stamp.** `IsOnDemandSacDef` (SpellEffects.h): a one-colour lump (`sac_for_mana_amount >= 2`, no
  `produces`, no pinned colour, no {T}-creature / haste rider, not an outlet: Lotus Bloom, Black Lotus by card data)
  or a land crack that eats another land (`sac_for_mana_victim_land_subtype`: Lake of the Dead). Scoped per game by
  the deck stamp `GameState::deck_has_ondemand_sac` (mainboard only; default TRUE for unstamped states), so
  FiveColour's conjured Black Lotus keeps the Garth route.
* **Retired action.** `CollectActions` emits no SacForMana for such a source; the pre-pass / colour fold / dup
  clause / Dragonstorm prefix code is untouched and simply never sees one. `MTG_IDLE_CRACK_FOLD` /
  `FoldIdleLandCracks` deleted (heurarm slot, counters and test hook with it).
* **Payer.** `TapForCostShared` (ManaPayment.cpp) is now a wrapper over the old body (`TapForCostSharedCore`), active
  only for the outermost payment (not a refine probe, not the oracle's float-only / scripted pass): (1) pay
  ordinarily with every crackable land HELD; (2) else a land's plain tap, kept when the rest of the line
  (`g_line_unpaid_cost`) still fits `AvailableManaPool` afterwards; (3) else crack, one source at a time (lands,
  then lumps; a lump's colour = the cost's shortest pip, then the line's remaining pips, the hand's, WUBRG),
  retrying after each; (4) else a stranding plain tap still pays this cost. Trials run on copies under
  `RevealLogPause`, the winning recipe is replayed on the real board. The surplus floats (`floating_mana`).
* **Supply at the gates.** `OnDemandSacSupplyNow` / `AddOnDemandSacToPool`: a lump as `amount` wild, a land crack
  as its delta over the plain tap in its colour, one per available victim. Added to `AvailableManaPool`,
  `AvailableManaPoolNoAttackers`, `BuildNonCreaturePool`, `BuildColorFeasibility`, `ComputeAvailableColors` and
  `UntappedManaUpperBound`.
* **Batch prepay.** Unchanged: it already declined on a non-empty float, so a crack line always paid per cast; now
  the batch pays a line that needs no crack, and a line that does falls to the per-cast payer, which cracks.
* **Viewer / replay.** Human auto-payment runs the same wrapper. `viewer_protocol_check.py` matches a recorded
  Lotus / Lake crack (an action with `sacout` or `float_color`) by the §2a "plan minus its implicit pay-sac" tier.
  Open: the Lake has no pre-tap CRACK face in the pay-click flow (the plain {B} only); Lotus keeps
  `Route::SacAction`.
* **Tests.** `test/unit/test_shadowborn_lakeprof.cpp`: not cracked when lands pay (Lake held), cracked when needed
  with the surplus floating, crack and plain tap exclusive, Lotus kept when lands pay and paying a colour no land
  makes, no SacForMana plan and the crack-needing cast still offered.
