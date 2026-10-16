# The land-search cluster (Landfall deck, 2026-10-15)

Self-contained record of how "search your library for land(s)" is modelled, what is searched, and
what is knowingly NOT yet expressible. Code: `src/core/LandSearch.h` (pick generator + resolution +
the activation / attack-trigger / in-combat resolvers), the pins beside `ShuffleAfterSearch` in
`src/core/SpellEffects.h`, the axis fan at the end of `AppendSubdecisionAxes` (TurnSolver.cpp).

## Cards and fire sites

| card | spec | fire site |
|---|---|---|
| Rampant Growth | 1 basic, tapped | spell resolution (EffectHandler / apply_one) |
| Search for Tomorrow | 1 basic, untapped; Suspend 2--{G} | resolution; off suspend: `CastOffSuspend` non-permanent branch |
| Cultivate | 2 basic, 1 to hand, tapped | resolution |
| Evolving Wilds / Terramorphic Expanse | 1 basic, tapped | `PermAbilityMode::SacLandSearch` ({T}, sacrifice) |
| Myriad Landscape | 2 basic sharing a land type, tapped | `SacLandSearch` ({2}, {T}, sacrifice) |
| Khalni Heart Expedition | 2 basic, tapped | `PermAbilityMode::QuestSacLandSearch` (3 quest counters + sacrifice) |
| Primeval Titan | 2 ANY lands, tapped, optional | `FireOwnEtbTriggers` + `FireAttackLandSearch` (declare-attackers) |

Radiant Fountain and Haunted Fengraf are the two non-basic utility lands Titan can fetch; Fengraf's
random graveyard return is `gy_return_at_random` (ApplyGraveyardReturnAbility).

## What is searched (no greedy in the window)

* `LandSearchPicks` enumerates every legal COMPLETE pick at resolution, folded only by two sound
  identities: copies / behavioural twins (`behaviour_identity`: Evolving Wilds == Terramorphic
  Expanse), and "up to N" -> the MAXIMAL picks (one card removed from an otherwise identical CRN
  future; the human may stop early). The provider only ORDERS it (`LandSearchPickOrder`). No width.
* `Plan::land_search_pins` -- one entry per in-plan land-search ordinal (spells, Titan ETB incl. a
  Titan a Genesis Wave will put, activations). Full product across ordinals up to 64 variants,
  additive (one-hot per ordinal) past it.
* `Plan::attack_land_search_choice` -- Titan's attack trigger, packed per attacking Titan; the
  in-combat Khalni cracks continue the same packed stream.
* `Plan::upkeep_land_search_choice` -- Search for Tomorrow off suspend, packed per arrival.
* `Plan::combat_crack_choice` -- the in-combat Khalni crack (bitmask over Khalni-type permanents).
* `Plan::land_search_act_slot` -- WHERE in the cast sequence the land-search activations run, in
  `Plan::land_slot`'s encoding: -1 the trailing pass (default), 0 before every cast, k after the k-th
  landfall-watcher cast (see gap 1).
* A Karoo-bearing pick's BOUNCE (gap 3): extended pick-list values on the same pins; a Genesis Wave's
  put Karoo: `Plan::bounce_choice` as the plan's put pin.
* Search for Tomorrow hard-cast: one cast variant per land-put CREDIT class (gap 2), each restricting
  its own pick.
* Human play: sequential `tutor_etb` prompts per land (DECISIONS.md), the `landsearch=` verb for the
  activations, the `dig` prompt for the in-combat crack, the ordinary `bounce` prompt for a put Karoo.

## Collapses that are PROVISIONAL (need user sign-off)

1. "Up to N" / "you may search" (Titan) collapsed to the maximal picks / to searching, for the
   SEARCH; the human keeps both. Argument: the extra land is one card removed from an otherwise
   identical future (Library::ShuffleByKey keys each card independently). Corner: Cultivate's hand
   basic can force a cleanup discard -- our own choice, equivalent to not fetching it.
2. Search for Tomorrow's "you may cast it" off suspend collapsed to cast, for the search AND the
   human (declining gains nothing; only the shuffle differs).
3. Instant-speed activation timing (Evolving Wilds, Terramorphic, Myriad, Fengraf) collapsed to the
   trailing pass of our main phase -- inert vs the passive opponent. Khalni's combat window is
   modelled (below); its opponent-turn window is not (equivalent to a main-1 crack next turn except
   that the lands would untap -- they enter tapped either way, so a main-2 / end-step crack gives the
   same next-turn board).
4. Haunted Fengraf's random card is a seeded, deterministic draw the clairvoyant search reads (the
   Gamble model), keyed on (game seed, turn, eligible count) in canonical order.

## Known gaps (DEFERRED -- each is a missing capability, not a heuristic)

1. *(Closed 2026-10-15, follow-up (b); re-expressed as a POSITION when the land drop became one.)*
   **Activation-vs-cast ORDER.** Board activations run in the TRAILING pass, after the plan's casts,
   so "crack Evolving Wilds / Myriad / Khalni BEFORE casting Avenger of Zendikar or Phylath" (more
   lands -> more Plants from the ETB count) was not one plan. `Plan::land_search_act_slot` carries the
   activations' position in exactly `Plan::land_slot`'s encoding (-1 trailing, the default; 0 before
   every cast, at the Lotus/Suspend point before the batch prepay and after a land-first drop; k
   right after the k-th `TurnSolver::PutsLandfallWatcher` cast, AFTER the land when both sit at k, so
   a Khalni the land just filled is crackable; an unreached k falls back to the trailing pass). Both
   worlds apply them through ONE body (`TurnSolver::ApplyLandSearchActs`) at the same points --
   ApplyPlanDirect's pre-cast pass / `slot_hook` / trailing pass and AIEngine::TakeTurn's twins --
   and a position plan's tied-rank watchers go first (`LandSlotWatchersFirst`) as for the land.
   Emitted for every plan that both activates a land search and casts a card whose ETB counts lands
   (`etb_tokens_per_land`): position 0, and 1..W-1 for its W watcher casts ("Baloths, crack, Avenger
   counting the crack's land"); W only when a land-counting cast is not itself a watcher (with every
   land-counter a watcher -- the whole current pool -- position W is the trailing pass up to where
   tapped lands and a shuffle fall among casts that read neither, an identity fold).
   LOCKSTEP NOTE (Courser of Kruphix): a crack's shuffle can leave a new playable land on top. The
   rollout's per-cast window (end of apply_one) runs BEFORE its slot hook, the executor's
   (`put_in_hand_armed`) AFTER `exec_slot_hook`, so the executor freezes the cast window's library-top
   verdict across a positioned crack (`rdb_top_verdict`); the shuffle then arms site 10 through the
   SECTION window in both worlds. Remaining boundary: no position twin inside a breakpoint
   continuation, and none for a plan without a land-counting ETB (an ordinary landfall watcher already
   prefers cast-then-crack, the default). Human play orders freely (`human_action_order`).
2. *(Closed 2026-10-15, branch `landfall-gaps`.)* **Search for Tomorrow hard-cast mana.** The untapped
   basic a HARD-CAST Search for Tomorrow puts was not credited to the same plan's later casts, so
   "Search for Tomorrow, then Rampant Growth / Khalni Heart Expedition paid with the fetched Forest" was
   unenumerable. Now (generic on the params: `LandSearchPutsUntapped` -- an instant/sorcery
   `land_search_on_resolve` search that puts at least one land untapped):
   * **The credit rides `Action::rock_mana`** (CollectActions' LAND-PUT CREDIT post-pass), so every
     producer consumer already prices it: "a rock never funds its own cost", the odometer gain terms,
     the colour-presence widening, `FirstUnpayablePos`. Both real-payment walks
     (`SubsetPayableWithFilters`, `SubsetPayableSequential`) put the basic itself on their scratch board
     after the cast paid (`PutLandPutCreditOnScratch`); the funding-order hoist treats the cast as a
     producer in both apply worlds (`IsFundingProducerCast` in `ApplyEtbTreasureFundingOrder` /
     `PayableCastOrderCore`); the whole-turn prepay folds its cost without crediting it (the
     ETB-Treasure route), so a line the board pays alone is untouched and a line that needs the basic
     declines to the per-cast payer, fed SfT-first.
   * **The pick and the credit are one decision.** The cast is emitted once per distinct CREDIT CLASS
     of its legal picks (`LandSearchCreditGroups`: a Forest class credits {G}, a Mountain class {R}),
     front class first, and each variant's resolution is RESTRICTED to the picks earning its credit
     (`LandSearchCreditFilter`, read by `PerformLandSearch` through `apply_one`'s `cast_land_put` in the
     rollout and `StackEntry::land_put_credit` in the executor). The variants partition the picks, so
     no pick is lost and no colour is promised that the chosen basic cannot make; the in-plan pin then
     indexes the restricted list. `plan_signature` (`#LP`) and `BpCandFingerprint` key the class.
   * **Fail toward not crediting.** A pick is creditable only when every battlefield land of it is a
     basic with no enters-tapped rule; one uncreditable pick, or no legal pick (no basic left), and the
     cast carries no credit at all.
   * **Scope.** Decision space (searched collect, executor) emits every class; a rollout leaf keeps the
     front class only (the phyrexian precedent), which resolves the provider-front pick the playout
     always took. Human play carries no credit: the basic is the human's prompt (its default is the
     credited pick), and the segment re-prompt casts off the fetched land.
   * **Residual (disclosed).** The classes are read off the enumeration-time library: two land
     searches in one plan racing for the LAST copy of a class can leave the second one's restriction
     empty, which then falls back to the unrestricted pick (never a whiff) and the enumerated credit was
     optimistic for that plan (the apply is exact: the cast it funded strands). Crop Rotation
     (`tutor_land_to_battlefield`, Creature Giving) does NOT share the mechanism: its target is a
     separate, still-inert pin (below); the right fix there is to route it through `PerformLandSearch`
     (a 1-land any-land untapped spec), which would inherit the pick axis, this credit and gap 3's
     bounce at once -- its own item with its own rebaseline.
3. *(Closed 2026-10-15, branch `landfall-gaps`.)* **A put Karoo's bounce target** took
   `BounceSearchCandidates`' front for every Karoo an effect PUT (Primeval Titan's ETB / attack, Genesis
   Wave) -- an in-window greedy pick. Now searched, the provider still narrowing:
   * **Land-search puts: the extended pick list** (`LandSearchPinDecode`). After a list's W picks come
     `kLandSearchBounceStride` (= kBounceSearchWidth - 1) entries per Karoo-bearing pick
     (`Slot::karoo_picks`): value `W + r*S + (b-1)` is "the r-th Karoo pick, whose first Karoo returns
     candidate b of `BounceSearchCandidates` at the bounce" (clamped there). Every value below W means
     what it always did, and a list with no Karoo IS the plain list. The sub-choice rides the pick's
     own pin -- the in-plan `land_search_pins` (Titan cast or put by a Wave) and the packed attack pin --
     so it is ONE axis with the pick (no cross product), and every plan identity, state key and the
     combat-pin share already carry it. `PerformLandSearch` installs the bounce around the Karoo's pass-2
     extras; an unpinned fetched Karoo shields the plan's own put pin (`ScriptedPutBounce(-1)`).
   * **Genesis Wave: `Plan::bounce_choice` as the put pin.** On a plan whose land drop is NOT a Karoo,
     `bounce_choice` is the first non-search put Karoo's bounce (`PutBouncePinOf`), installed for the
     whole apply in both worlds (`ScriptedPutBounce` in `ApplyPlanDirect` / `AIEngine::TakeTurn`,
     ALWAYS, -1 included, so a nested apply never inherits it; `ContPins::put_bounce` in a continuation;
     the breakpoint prefix snapshot carries its consumption). Fanned in the bounce block of
     `AppendSubdecisionAxes` (base plans, one axis).
   * **Sizing** (`PutKarooBounceBound`): an upper bound on the narrowed width that mirrors
     `BounceSearchCandidates` -- distinct returnable names (+ one per depletion land), tightened to "one per
     tapped clean name, or ONE with no land in hand" when a tapped land that certainly re-enters
     untapped is proved present (Titan's forced-tapped partner; for the Wave, a cost no non-clean
     source can cover). `PlanMayHoldLandAtBounce` is the "land may be in hand" superset.
   * **Human play** was already right: every Karoo bounce, put or played, reaches the ordinary `bounce`
     chooser (`BounceKarooLand`), in main phase and in combat; unit-pinned for a Titan put.
   * **Cost, and the put-bounce fold** (`PutBounceFold`, TurnSolver.cpp; `MTG_PUT_BOUNCE_FOLD`,
     default ON, an exact fold). The width a put bounce really has is decided by which lands are TAPPED
     at the bounce -- for the attack trigger, by main 1's payments -- so the fan cannot size it tightly,
     and on Landfall it is almost always 1 (the USER rule or the tapped-clean rule decides it): measured
     over 240 games (`MTG_BOUNCE_STATS`), 99% of the pinned put bounces CLAMPED onto an index a sibling
     had already resolved, and gap 3 cost **+2.4% to +10% units over gap 2 alone with no play change**.
     The combat-pin fold and the post-apply dedup did refuse to SCORE those variants, but only after
     each had paid its unit, its apply and (attack) its combat. The fold declines them BEFORE the unit:
     a probe bound to the candidate's own state object records, at each family member's resolution,
     the pick list's shape, the pick, the narrowed bounce width and the index taken; a later variant
     whose value decodes against that shape to a (pick, min(b, width - 1)) a sibling already resolved
     IS that sibling's combat / apply.
     - **Attack mode**: in `SolveWithLookahead` it rides the combat-pin MAIN-PHASE SHARE (every member
       starts combat from the cached base state); in `FSLineWin` it proves its base's apply pin-blind
       (no pending node, no combat-pin read, no unit, no breakpoint) and finishes the skipped variant's
       post-apply key from the base's pin-free prefix (`FoldLandSearchPinValues`).
     - **In-plan mode** (the Titan ETB sub-choice): members whose apply spent no unit and stopped at no
       breakpoint node; the recorded apply's breakpoint-counter deltas (`ApplyTrace`) are replayed into
       the walker's records by the skip.
     - The skip leaves exactly what the skip it pre-empts left (the per-apply resets, the walker / NOBP
       / stillborn records, the probe's no-win value, the beam refund, the overrun guard's count), so
       the only change is the unit no longer charged -- a budget RE-PRICE, inert at unlimited budget.
     - **Measured** (Landfall, one pooled batch with per-job flags: d3/b10 and d5/b20 x seeds 8008 /
       9009 x 60 games, plus d2/b0 x 8). Verify mode: 4,875,783 would-be declines, every one skipped
       by the fold it pre-empts (bad = 0), and the verify arm equals the off arm in digest and units.
       On vs off: 0 better / 0 worse, 1 play-changed game (same win turn), units **0.946** (0.927 -
       0.956); the unbudgeted d2 digest is identical. With the fold, gap 2+3 reproduces the gap-2-only
       digests on all four budgeted configs (gap 3 changed no Landfall play there: the put width was
       1 nearly every time), so the units it still adds over gap 2 alone are pure overhead:
       **+0.2% / +2.1% / +2.6% / +5.1%** (d5 s8008 / d3 s8008 / d5 s9009 / d3 s9009), down from
       +5.0% / +7.2% / +9.9% / +10.0% (before the fold the waste re-routed one d5 s9009 game, same win
       turn; the fold restores it).
     - **Residual, attributed** (d3 s9009, identical play, gap 2 vs gap 2+3+fold, +533k units):
       `fs_bp_node` +347k, `fs_pre` +160k, the wave sites +27k, `la_cand` +1k. The node share is the
       BREAKPOINT-NODE children of a PENDING attack variant: its pending key carries its attack pin, so
       every variant hosts the whole continuation list, and the children loop has no combat-pin fold,
       so a clamped variant's children are scored in full. DEFERRED: a per-child twin of this fold
       (record each child k's resolution at the attack ordinal, decline a variant's child k that
       decodes onto a sibling's) would close it; it needs the pending prefix proven pin-blind as the
       FSLineWin base test does.
     - Not covered (still dropped after their unit by the old mechanisms): members whose apply consumed
       a unit, stopped at a breakpoint node, or (attack, FSLineWin) reached a breakpoint; variants
       deviating at two ordinals at once; a Genesis Wave's put pin (`Plan::bounce_choice` with no
       `bounce_group`, so `BounceFoldMemo` -- the drop bounce's fold -- does not see it either).
   * **Residual (disclosed).** One sub-choice per pick (a pick holding two Karoos returns the second's
     front); one Wave put pin per plan (a second non-search put Karoo, or a Wave beside a Karoo DROP,
     takes the front); sizing reads the enumeration-time library / top X (the Titan-via-Wave caveat);
     the packed attack slot's 6-bit cap (gap 6) now also bounds the extended values; the unpruned audit
     reaches at most `kBounceSearchWidth` candidates of a put Karoo (the stride).
4. **Additive across axes.** The land-search axes follow the engine's one-axis-at-a-time convention:
   a land-search pick variant never also carries another axis's deviation (a scry, a DROP bounce, a
   Phylath target rank), and an in-combat crack variant keeps the Titan's own attack pick at the
   front. Past 64 product combinations the in-plan pins are one-hot per ordinal. (A PUT Karoo's bounce
   is not another axis: it is a sub-choice of the pick, gap 3.)
5. **Cast ordinals are sized at the widest cast source** (the canonical cast bucketing is not
   reproduced at fan time); a narrower source clamps to its last pick at resolution (a duplicate the
   post-apply dedup skips).
6. **Packed-pin width cap.** A packed state pin holds 5 ordinals of 6 bits (64 picks each); a deck
   whose Titan sees more than 64 identity classes of land pairs would clamp. Landfall's is ~25.
7. **Lands with no mana ability.** Evolving Wilds / Terramorphic are `template: custom` so no mana
   site counts them; a few IsLand()-based heuristics (keep-for-main-2 count, end-of-main sweep, the
   anti-lifegain tutor source count) still read them as lands -- optimistic, not wrong.

## Not touched here

* Crop Rotation's land-to-battlefield tutor pin is inert in resolve mode (Creature Giving): a real
  in-window front pick, its own item with its own rebaseline.
