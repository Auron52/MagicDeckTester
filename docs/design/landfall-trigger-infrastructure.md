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
(Courser), `landfall_quest_counters` (Khalni; the "may" resolved YES by dominance). ETB:
`etb_tokens_per_land` (+ `_basic_only`, through the shared `IsBasicLandCard`).

## 3. Searched axes added

* `Plan::land_after_casts` -- land-drop TIMING. Cloned from each deferred-drop plan (casts enumerated
  without the land) once per offered land option, iff the deck holds a landfall card and a cast in the
  plan puts a landfall watcher onto the battlefield (the card itself, or a Genesis Wave). Applied
  through the Karoo deferral path in both worlds. Line verb `landafter=`.
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

1. **A land in the MIDDLE of the cast order.** `land_after_casts` plays the land after ALL the plan's
   casts, so "cast Khalni Heart Expedition, play the land, then cast a 3-drop with the new land's
   mana" is still not expressible. The natural route is a breakpoint after the watcher cast whose
   continuation plays the drop (the deferred drop is already reserved), but continuation plans play
   their land before their casts (`bp_play_searched_land`), and the axis is deliberately not emitted
   inside a continuation derivation (`g_bp_enum_depth > 0`).
2. **Fold boundary of the timing axis.** It is emitted only when a cast puts a landfall watcher. For
   the current pool that is sound; a future NON-watcher cast that makes a landfall payload's target
   subtype (a non-landfall Plant maker), or a "whenever you gain life" watcher cast beside Courser of
   Kruphix, would need a clause in `puts_landfall_watcher` (TurnSolver's EnumeratePlansWithLandUncached).
3. **Last-known information** for a watcher that leaves mid-cascade is not modelled (its later
   landfall triggers are skipped). Unreachable today: no landfall payload removes a permanent.
4. **Omnath's "any target"** resolves to the opponent's face and is not offered to the human (the
   `dies_trigger_damage` precedent). PROVISIONAL, user sign-off pending.
5. **A PUT Karoo's bounce** (Genesis Wave, a ramp put) takes the provider's front pick with no plan
   pin -- found by the Landfall card review, pre-existing, not addressed here.
6. **Courser + Primeval Titan's ATTACK shuffle -- no autonomous main-2 drop (KNOWN GAP, USER Q4).**
   Titan's attack trigger searches and shuffles during combat; with Courser out and the drop unused in
   main 1, the post-combat top can be a newly playable land whose landfall triggers pay off next turn.
   The autonomous engine has no searched main 2 for this deck, so the line is unfindable there (human
   play has it: the viewer offers the main-2 drop). Per the repo's "delete, don't lever" rule it is
   NOT built behind a default-off lever; the shape that would close it is a restricted main 2 like
   `DigSecondMainLive` (a deck constant + a per-turn stamp from the attack-trigger shuffle + the drop
   open + a newly playable top), measured, and adopted only by the user. Do not add the deck to
   `DeckUsesSecondMain` for it (a full main 2 every turn cost Snow 1.96x / Fungus 4.25x).
