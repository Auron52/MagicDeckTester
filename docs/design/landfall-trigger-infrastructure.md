# Landfall trigger infrastructure (2026-10-15)

Status: BUILT (uncommitted at writing; landed with the Landfall deck's payoff cards). This doc is the
standalone record of the contract, the two searched axes it added, and what is still deferred.

## 1. The chokepoint contract

`OnLandEntered(state, controller, land_number, target_snapshot = nullptr)` in
`src/core/SpellEffects.h` is THE landfall dispatch. It runs exactly once per land that enters the
battlefield, after the permanent is on the battlefield with its as-enters state applied, and fires
every landfall watcher `controller` controls once for that land. It reads nothing about the land
beyond its controller, so a land that has already left again (a Karoo that bounced itself) still
triggers.

Three routes call it. A new land-entry effect must reuse one of them:

| Route | Function | Covers |
|---|---|---|
| 1 | `PlayLandFromHand` (`src/ai/LandPlay.cpp`) | every PLAYED land in every world: the executor's searched and greedy drops, the combo route, the rollout's `PlayLandByName`. A fetchland's drop fires for the fetchland itself first (the engine collapses its battlefield stay, the real card does enter), then route 2 fires for the fetched land. |
| 2 | `EnterLand` (`src/core/SpellEffects.h`) | every land PUT by an effect: `PerformFetch`, `PerformLandTutorToBattlefield`, Genesis Wave, and every ramp spell / ability built on `EnterLand`. Signature is backward compatible: `fire_landfall` is a new trailing defaulted parameter. |
| 3 | `FireEtbWatchers` (universal enter cascade) | any land entrant passed to it: a blinked land, a generic put (`PutCardOntoBattlefield`, `ApplyPutFromHand`, `PerformTutorToBattlefield` with a land type), Kaldring's graveyard play. |

Never route one entry through two of them. The two opt-outs (`EnterLand(..., fire_landfall=false)`,
`FireEtbWatchers(..., fire_landfall=false)`) exist for exactly one caller, `PerformGenesisWave`.

Per-game gate: `GameState::deck_has_landfall`, stamped from `DefHasLandfallTrigger` (the same
disjunction as the per-definition `CardDefinition::landfall_watcher` byte). A deck with no landfall
card pays one bool test per land entry.

### Simultaneity

* `PerformGenesisWave` places every entrant (pass 1), snapshots the board (the legal targets of a
  TARGETED landfall trigger, CR 603.3d), then resolves in the dominant CR 603.3b order when a landfall
  watcher or a land-counting ETB is involved: enter-watchers + creating/counting own ETBs (2a),
  landfall per placed land (3, iterating the PLACED list so a land already gone still triggers),
  then the board-reading / board-shrinking ETBs and each land's own triggers (2b: Terastodon, Acidic
  Slime, Shriekmaw, Craterhoof, Karoo bounce, land lifegain). Every other game keeps the legacy
  reveal-order pass (SelesnyaLifegain is byte-identical).
* A put of several lands and nothing else may call `EnterLand` once per land: every payload in the
  pool commutes and none makes a permanent another landfall trigger can target. A future payload that
  makes a target-subtype permanent breaks that and must take the snapshot shape.

## 2. Payload params (`CardParams`, LANDFALL TRIGGERS block)

`landfall_creates_tokens` + `landfall_token_*` (Baloths, Omnath; the colour string may hold several
letters), `landfall_counters_each` + `_subtype` (Avenger; the "may" resolved YES by dominance),
`landfall_target_counters` + `landfall_target_subtype` (Phylath; searched target), `landfall_lifegain`
(Courser), `landfall_quest_counters` (Khalni; the "may" resolved YES by dominance),
`landfall_double_self_counters` (Mossborn Hydra, 2026-10-22: the keyword action DOUBLE, CR 701.10e,
through the shared `DoublePlusCounters` -- a "put", so a counter doubler applies again). ETB:
`etb_tokens_per_land` (+ `_basic_only`, through the shared `IsBasicLandCard`). As-enters:
`enters_with_plus_counters` (Mossborn Hydra's one counter; a CR 614.1c replacement applied at the top
of `FireEtbWatchers`, and in pass 1 of a simultaneous multi-put such as Genesis Wave, so a
co-entrant's trigger resolving first already sees it -- `EntersWithPreappliedScope`).

## 3. Searched axes added

* `Plan::land_slot` -- the land-drop POSITION in the cast sequence (was the bool `land_after_casts`,
  first or after all). `0` first (default); `-1` after ALL the casts (cloned from each deferred-drop
  plan, whose casts were enumerated without the land, once per offered land option); `k >= 1` right
  after the k-th cast that puts a landfall watcher onto the battlefield (cloned from each LAND-FIRST
  plan, whose casts may spend the land's mana, once per watcher cast). Positions only ever follow a
  watcher cast: moving the land past a non-watcher cast triggers the same watchers and gives its mana
  to fewer casts. The ONE predicate is `TurnSolver::PutsLandfallWatcher` (a landfall card, or a mass
  put such as Genesis Wave): the enumeration emits a position per such action and both worlds' cast
  loops count exactly those actions (ApplyPlanDirect's slot hook in `apply_plan_actions`, armed for the
  top-level section only; AIEngine::TakeTurn's `exec_slot_hook` in every main cast loop) and play the
  deferred land through the Karoo deferral path; a position whose watcher never comes plays after all
  the casts. A positioned plan dispatches its TIED-rank watcher casts first
  (`TurnSolver::LandSlotWatchersFirst`, before the stable rank sort -- only the otherwise-arbitrary
  plan-order tie-break moves, never a provider rank), so "Khalni, land, Rampant Growth" is reachable
  whichever card the hand listed first. Gated on `deck_has_landfall` + a watcher cast in the plan, so
  every other deck is byte-identical. Line verbs `landafter=` (-1) and `landat=<k>` / `landat=all`.
  * **An unpayable prefix is not emitted (claude-play sweep flag 1, 2026-10-16; 13 of 16 games).** A k
    clone inherits casts enumerated WITH the land's mana, so "Khalni Heart Expedition, then the Forest"
    was offered with one Forest in play. Applied, BOTH worlds drop the unpayable cast and play the land
    anyway -- the slot hook counts the dispatch ATTEMPT in `ApplyPlanDirect` and in `AIEngine::TakeTurn`
    alike, so the search never scored a phantom quest counter (unit-tested on the rollout's apply) --
    and the rest of the plan runs (gi10: Phylath dropped, the Khalni crack still cracked). The
    enumeration now applies each land-first base plan ONCE at its last position under a
    `SlotPrefixProbe`: the watchers dispatched before its first failed prefix cast (`s`) bound every
    position at once (positions 1..s dispatch exactly that prefix with no land yet; any position above
    s drops the same cast before its land), so positions above s are not emitted. Sound: the dropped
    line realises "casts minus the dropped ones, land after the same watcher attempt", which is the
    position variant (or the land-first plan) over the smaller cast set, enumerated in its own right;
    its trailing activations (a crack) run after the land in both; the only possible difference is
    WHICH lands it tapped -- the payment allocation, not a searched decision anywhere (the standing
    mana-payment exemption). Kept as before (every position emitted, the degenerate ones merely wasted
    and honestly scored) where a later fan-out could buy the missing mana back
    (`PositionPrefixChoiceMayAddMana`: an untapped land-search put such as Search for Tomorrow, a
    Treasure minter, a sac-land cast under the sac axis, the TapReserve audit, any breakpoint the plan
    opens) and inside a breakpoint continuation's derivation (`g_bp_enum_depth > 0`: applied there by
    `apply_continuation_plan`, which has no whole-turn prepay, so the top-level probe is not that
    world's answer). A new mana-adding resolution choice must join that predicate.
  * **k == -1 twins fold at the post-apply dedup (sweep flag 2; 7 of 16 games).** "Land after the
    LAST watcher" and "land after all the casts" over one cast list reach byte-identical states when
    nothing after that watcher can see the position, yet their fingerprints (`LSLOT k` / `LAFTE`) and
    human signatures (`|at<k>` / `|at-1`) rightly differ, because they are only SOMETIMES the same
    line: a trailing activation (a crack the land filled), a land-search activation's position, a
    site-9 continuation or a land the apply made activatable all sit between the k-th cast and the -1
    point, and the -1 clone does not apply the watchers-first tie-break, so even the dispatch order
    (and with it every sub-axis pin ordinal) can differ. An enumeration-time "nothing follows the k-th
    cast" judgement is therefore unsound; the fold is the post-apply STATE identity instead:
    `PlanDupSkippable` now covers every `land_slot != 0` plan, so a positioned plan that lands on an
    earlier sibling's exact state costs one apply and no rollout (the same key-completeness stake
    every axis variant makes), per candidate, children included. The human menu hides the positioned
    twin of an after-all plan with the identical post-apply state (`TurnSolver::
    PositionedLandTwinIndex`, a `hide_bundle` collapse in `WriteDecisionJson`): hidden, not removed,
    so indices, `landat=<k>` matching and the uncapped reference checker are unchanged; the survivor
    is the after-all plan, the label the viewer's `landafter=` builds.
* `Plan::landfall_target_choice` -- a whole-turn rank policy into the provider-ordered,
  identity-folded targets of a targeted landfall trigger (`LandfallTargetFoldKey`,
  `DecisionProvider::LandfallCounterTargetOrder`). Turn-scoped, folded into the solve memo key while
  live.

## 3b. The library top as a land-drop source (Courser of Kruphix, 2026-10-15)

"Play with the top card of your library revealed. You may play lands from the top of your library."
Design: `logs/landfall_drafts/_courser_design.md` (gitignored draft); this section is the standalone
record of what was built.

* **One definition, both worlds.** `TopLandPlayStaticSource` / `PlayableTopLandNumber` /
  `LibraryTopVisible` (`src/core/SpellEffects.h`), gated on the deck constant
  `GameState::deck_has_top_land_play` (stamped from `play_lands_from_library_top`, Garth holds it
  open, default TRUE for an unstamped state). The permission is the SAME land drop (2021-03-19 ruling).
* **One land body.** `PlayLandFromLibraryTop` (`src/ai/LandPlay.cpp`) and `PlayLandFromHand` share one
  templated core; the top path pops the card first (a raw pop, not a draw), the hand path keeps its
  exact erase points (byte-identical). Every land ETB and the landfall dispatch fire from the core.
* **A source bit on every land choice.** `Plan::land_from_top`, `Action::land_from_top` (the
  continuation record), `LineSpec::land_from_top` (`landtop=`). Every site that carries `land_face`
  carries it: `PlayLandByName(..., from_top)` and `AIEngine::TryPlaySpecificLand(..., from_top)` refuse
  a top that is not that land instead of substituting a hand copy; the deferred Karoo / land-after
  drop carries `karoo_from_top`. Folded into every plan identity key (plan hash, BpCandFingerprint,
  SamePlan, IsApplyEmptyPlan, the cast-ordering signature, the PlanDom key and census, the tutor-axis
  cache key, the three human-menu keys in `main.cpp`), and handled by the two hand-index maps
  (`build_hmap`, the new-only filter's `land_pick_index`): a top land never leaves the hand.
* **Branched, never folded.** The top is its own land option in `EnumeratePlansWithLandUncached`
  (both MDFC faces, the same fetch fan as a hand fetchland, the Karoo self-bounce prune; a top Karoo
  stays ON TOP while the deferred plan's casts are enumerated). Top vs a same-name hand land is not an
  identity fold (different library, different hand, different CRN reshuffle copies) and not a sound
  dominance, so both are scored; the provider may only ORDER (`TopLandSourceFirstOnTie`, default true,
  the comparator's last key: a top play keeps the hand copy).
* **A new top is a new option (breakpoint site 10).** `TurnSolver::TopLandNewlyPlayable(snapshot,
  state, drop_reserved)` -- a playable top that was not the snapshot's, drop open and not reserved --
  is OR-ed into site 10's per-cast window (`apply_one` / `put_in_hand_armed`) and its section window
  (outside the hand-entry sequence pre-filter), in both worlds. `PlanOpensBreakpoint` gains the
  matching site-10 clause (drop still open after the plan's own land, a play-from-top permanent out
  or cast by the plan, and the plan casts something or plays from the top) -- a superset, so the
  continuation is SEARCHED, never `BpUnbranchedCanon`'s front pick. The new-only continuation filter
  keeps a top-sourced continuation unconditionally (no pre-site top snapshot is bound; the safe
  direction). Measured on the fixtures: `MTG_BP_CANON_AUDIT` applies no canon default.
* **The permitted greedy sites** (`GreedyLandChoice`: the d0 runner `AIEngine::TryPlayLand` and the
  beyond-horizon `SimulateLandPlay`, both of its branches) see the top as a candidate in the same
  passes; with no playable top they are exactly the hand ranker. The enumeration's last-resort
  `greedy_land_name` tie-break names a source too.
* **Main 2.** `GameState::StampMain1Hand(top_land_number)` marks a top that was already a playable land
  as main 1 began, so `HeldLandBarredFromMain2` bars it exactly like a held hand land.
* **Viewer.** `me.library_top {name, number, kind, playable_land}` only while `LibraryTopVisible`;
  the GUI renders it on the library pile and a click queues `landtop=<name>` (`linebuild.js`: the
  land toggle keys on (name, source), the top caps at one, `stampPlanNums` stamps its number);
  `landat=all` is the timing modifier for a top land after the casts. `land=` / `landafter=` keep
  naming a HAND copy, so every saved reference replays unchanged.
* **Log.** `PLAY_LAND` carries `"from": "library_top"` (and folds a `T` into the digest) only for a
  top play; `boardAfter.libraryTop` is a non-digest per-phase snapshot while revealed.
* **Disclosed (USER questions, defaults taken 2026-10-15).** Q2: the clairvoyant search sequences
  "shuffle, then play the newly revealed top land" knowing the post-shuffle order (systemic, the same
  property as fetch-then-draw). Q3: exact ties order the top first. Q5: the log records the source
  plus the per-phase snapshot, no REVEAL line per top change.

## 4. Deferred (not built)

1. ~~**A land in the MIDDLE of the cast order.**~~ BUILT 2026-10-15 as `Plan::land_slot` (section 3),
   inside breakpoint continuations too: the position clones are emitted in a continuation
   derivation (`g_bp_enum_depth > 0`) as well, `bp_play_searched_land` HOLDS a positioned
   continuation land (ApplyPlanDirect's `cont_land`, owned by the continuation plan object and
   armed by `apply_continuation_plan` for exactly that continuation's cast section), the same slot
   hook plays it after the section's k-th watcher cast or after its casts, and it is RECORDED when
   played -- so the executor's commit-the-line replay (`replay_recorded` walks the record in order)
   plays it at the same point. The executor's re-derived continuation paths (resolve_draw_breakpoint,
   the pod chain, site 9) hold it the same way (`ExecContLand` / `exec_cont_land_*`). While a
   continuation's land is held the drop is RESERVED (a nested continuation's land stands down in
   both worlds, the Karoo reservation's rule). Fixture: `landfall_position_in_continuation`.
2. **Fold boundary of the timing axis.** It is emitted only when a cast puts a landfall watcher. For
   the current pool that is sound; a future NON-watcher cast that makes a landfall payload's target
   subtype (a non-landfall Plant maker), or a "whenever you gain life" watcher cast beside Courser of
   Kruphix, would need a clause in `puts_landfall_watcher` (TurnSolver's EnumeratePlansWithLandUncached).
3. **Last-known information** for a watcher that leaves mid-cascade is not modelled (its later
   landfall triggers are skipped). Unreachable today: no landfall payload removes a permanent.
4. **Omnath's "any target"** resolves to the opponent's face and is not offered to the human (the
   `dies_trigger_damage` precedent). PROVISIONAL, user sign-off pending.
5. ~~**A PUT Karoo's bounce** (Genesis Wave, a ramp put) takes the provider's front pick with no plan
   pin.~~ CLOSED 2026-10-15 (branch `landfall-gaps`): a land-search put (Primeval Titan's ETB / attack)
   carries the bounce as a sub-choice of its pick (the extended pick list), a Genesis Wave put as
   `Plan::bounce_choice` (`ScriptedPutBounce`); both searched, provider-narrowed, both worlds. The
   attack trigger's clamped sub-choices are declined before their unit charge by the exact put-bounce
   fold (`PutBounceFold`). See `land-search-cluster.md`, known gap 3, for the mechanism, its cost and
   its disclosed residuals.
6. **Courser + Primeval Titan's ATTACK shuffle -- no autonomous main-2 drop (KNOWN GAP, USER Q4).**
   Titan's attack trigger searches and shuffles during combat; with Courser out and the drop unused in
   main 1, the post-combat top can be a newly playable land whose landfall triggers pay off next turn.
   The autonomous engine has no searched main 2 for this deck, so the line is unfindable there (human
   play has it: the viewer offers the main-2 drop). Per the repo's "delete, don't lever" rule it is
   NOT built behind a default-off lever; the shape that would close it is a restricted main 2 like
   `DigSecondMainLive` (a deck constant + a per-turn stamp from the attack-trigger shuffle + the drop
   open + a newly playable top), measured, and adopted only by the user. Do not add the deck to
   `DeckUsesSecondMain` for it (a full main 2 every turn cost Snow 1.96x / Fungus 4.25x).
7. **Unpayable-prefix positions inside a breakpoint continuation (residual waste of sweep flag 1).** The
   prefix probe runs at `g_bp_enum_depth == 0` only. A continuation's positioned land is applied by
   `apply_continuation_plan`, which has no whole-turn batch prepay, so the top-level probe is not that
   world's answer and a continuation's degenerate positions are still enumerated (honestly scored,
   merely wasted). Closing it needs a probe through the continuation's own section (or a proof that the
   two payers agree on a dropped cast), measured like any other collapse.
8. **A PUT Terastodon's K for the human (sweep flag 3 follow-up).** The human now picks WHICH
   permanents Terastodon destroys, but a Terastodon PUT by Genesis Wave / Natural Order resolves
   K = the provider's projection (`EtbDestroyK`); only a cast fans K in the menu. "Up to three" for a
   put would need either a K prompt or a stop option in the per-target pick (the loyalty board-pick has
   no decline: a `-1` there means "the default"). USER question; default: K stays the projection.
