# Human play: apply the queued action order AS-IS (every queued step, the land included)

**Status 2026-10-22: the WHOLE queued segment is sequenced** -- every kind the viewer can queue, the
land drop and each Land's Edge discard included, through the `"**"` side-channel list (section
"2026-10-22" below). USER: *"The viewer should use exactly the order I give. Same for everything else
in the viewer."* The 2026-09-10 `"*"` form below is what references saved between those dates carry,
and it still replays exactly as recorded.

2026-09-10, EldraziDisplacerFlicker. USER report, verbatim:

> "the order is off for Emiel activations and Kitchen activations. This means that I cannot draw
> and then untap the kitchen without going to an extra breakpoint..."
> "Ideally the order I provide would be followed as-is."
> "It's up to me to make sure the order is correct."

Shipped in this repo as `MTG_HUMAN_LINE_ORDER` (default **ON**, `=0` restores the previous
cast-only pin). This doc records the confirmed mechanism, the shape of the fix, and the one piece
deliberately **left undone**.

---

## The mechanism, as measured (not inferred)

Three separate layers each discarded the human's order; only the third one mattered.

1. **`tools/play/linebuild.js`'s `encodeLine` groups tokens by verb** — every `cast=` first, then
   `vial=`/`retrace=`, then the other verbs. So `[crack the Clue, blink Emiel]` and
   `[blink Emiel, crack the Clue]` encode to the *same string*. This turns out not to matter,
   because:
2. **`TurnSolver::CheckLine` matches a line to a plan by MULTISET**, not by sequence. Confirmed at
   seed 1 / game 0 / turn 6: `cast=Clue Token;blink=Emiel the Blessed@10*1` and
   `blink=Emiel the Blessed@10*1;cast=Clue Token` both return `accept` on **plan 164**.
3. **...and plan 164's action vector is `[Emiel the Blessed, Clue Token]`, which is the only order
   the enumerator ever emits.** A census of all 184 plans at that decision found
   `(Mariposa, Emiel)`, `(Conservatory, Emiel)`, `(Emiel, Clue Token)` and never a reverse — the
   activations come out in **battlefield-index order**, i.e. the order those permanents happened to
   enter play. A fact about the past, with no bearing on the turn.

The apply then honours that vector: `ApplyPlanDirect` casts the hand spells (in `CastOrderLess`
order, or in vector order for a `searched_order` plan) and afterwards runs
`apply_trailing_activations(plan.actions)`, a single pass in vector order. `--cast-order` — the
side channel the viewer already used to pin the human's order across the two processes — only ever
carried **hand cast** names (`Plan::searched_order`), so activations were never in the pin at all.

**Where the extra breakpoint came from.** With `(Emiel, Clue)` the only expressible order, "crack
the Clue to DRAW, then blink to UNTAP the land" required committing `cast=Clue Token` alone, taking
the commit-line re-prompt, and committing `blink=Emiel...` as a second line. That re-prompt is the
"extra breakpoint". Nothing was stopping mid-line — the deck has no draw-engine card
(`is_draw_engine` / `stage_draw_break` are false for all 26 of its cards), and the perm-ability
draws (Mariposa's `{5},{T}` and the Clue's sac-draw) truncate nothing. The order alone forced the
split.

*(Kitchen and Conservatory are mechanically the same card here — `{4},{T}: Investigate`, param
`tap_investigate_cost` — so the reproduction on Conservatory/Clue is the reported case exactly.)*

## The fix

* **`LineSpec` is untouched.** The order does not travel in the line string, because the viewer
  validates in one process and commits by plan *index* in another. It travels in the existing
  `--cast-order` side channel, which already survives that hop **and** is already recorded into
  saved references (`cast_order` on the main-phase trace entry) and reconstructed by
  `viewer_protocol_check.py`'s `side_channel_args`.
* **A leading `"*"` marker** widens that channel from "the cast order" to "the whole line's order".
  No MTG card name is `*`, so it cannot collide; every reference saved before 2026-09-10 carries a
  cast-only list with no marker and takes the old branch verbatim. A **marker**, not an inferred
  "did this list happen to cover every slot?" test — inference is exactly how an old cast-only list
  would get silently promoted and shuffle a saved game's activations.
* **`AIEngine::ReorderPlanCasts`** widens its reorderable slot set from "non-sac hand casts" to
  "non-sac hand casts + every board activation" (`TurnSolver::IsTrailingActivation`) and sets
  `Plan::human_action_order` when an activation is actually in the sequence.
* **`ApplyPlanDirect`** then walks `actions` ONCE in vector order, dispatching each board
  activation inline where it stands through the *same* trailing dispatcher (a one-action slice —
  never a second implementation of any activation), and skips the trailing pass. The top-level
  `apply_plan_actions(plan.actions, ...)` call moved below the dispatcher's definition so the
  ordered cast loop can reach it; that is a pure code move (a lambda definition executes nothing).
* **`tools/play/index.html`** pins the full queued sequence whenever the queued entries and the
  matched plan's `actions` are the same multiset; otherwise it falls back to the historical
  cast-only pin. A plan carrying an action the line does not name (an implicit `SacForMana`, a Vial
  deploy) therefore keeps exactly the old behaviour — the queued order says nothing about those.

*(As of 2026-09-10 -- superseded by the `"**"` sequence below, which sequences every one of these:)*
Kinds that keep their canonical pass and are **not** sequenceable: the land drop, `ActivateVial`,
`SacForMana`, `Suspend`, `DigDraw` (a cycle commits its own line), sac-land casts,
`CastFromGraveyard` (retrace), `DiscardToLandsEdge`. Each runs in a loop of its own, before or
after the interleaved walk, and the viewer has no affordance to place them against the rest.

`MTG_LINE_ORDER_TRACE=1` prints the realised sequence (real applies only, never a rollout) — the
order is otherwise unobservable from outside the process, which is how a first cut of this work
read the search's last scored candidate as though it were the played line.

## 2026-10-22: the WHOLE queued segment, in queue order (`"**"`)

USER, 2026-10-22: *"The viewer should use exactly the order I give. Same for everything else in the
viewer."* A read-only audit of the human path found five places where the 2026-09-10 fix above still
let the engine choose the order; each was confirmed in code (and, for D, by a failing test) before it
was changed. The Karoo drop's position (`docs/design/karoo-drop-position.md`) was the precedent: a
queue position IS a timing.

### What was wrong

* **(A) The land's position was dropped.** `linebuild.js` wrote a position (`landat=` /
  `landafter=`) only on a decision that offered a land-after line (a landfall deck); everywhere else a
  land queued after a cast encoded `land=`, the viewer's pin left the land out, and `ApplyPlanDirect`
  played it first. "Cast Sol Ring, then play the Forest" played the Forest first. `CheckLine` matched a
  position only right after an ETB-landfall cast (`landat=<k>` naming any other cast graded
  legal_not_enumerated -- a reject for a legal line).
* **(B) Kinds the `"*"` pin could not sequence.** `ReorderPlanCasts` made slots of non-sac hand casts,
  Vial puts and the `IsTrailingActivation` kinds only. Every other kind ran in a pass of its own:
  a declared sac-for-mana, a suspend, a pre-pass convoke tap and a land-search crack BEFORE the line;
  sac-land casts, retrace casts, Land's Edge (one count, all at once) and the dig AFTER the ordered
  loop; Krenko's tap after every cast -- although the viewer queues `act=Krenko` in click order.
  Under `MTG_GARTH_ORDERED` (default on) a Garth activation dispatched through the trailing
  dispatcher applied nothing at all.
* **(C) The fallback dropped the activation order.** The viewer sent the `"*"` list only when the
  matched plan's actions were exactly the queued names; one implicit action (a self-sacrifice mana
  source the viewer offers no click for) dropped it to a cast-only pin, so the activations ran in
  enumerator order -- and with fewer than two casts nothing was pinned at all.
* **(D) The whole-turn prepay ate manual taps.** `BatchPrepayMainCasts` taps for every cast of the line
  BEFORE the ordered walk, where the manual taps fire. Test first:
  `test/scenarios/stompy_human_pretap_survives_prepay.json` (three Forests, two Llanowar Elves, the
  human taps Forest #1 for {G}) FAILED on the unfixed binary with `manual tap SKIPPED: 'Forest' is
  already tapped` -- the prepay had paid both Elves from Forests #1 and #2. The `"*"` and taps-only
  pins showed the same thing. Real.
* **(E) Mulligan bottoms and cleanup discards went in hand-index order.** The engine asks one decision
  per card and reads a hand index, so the viewer sorted the multi-select into DESCENDING index order
  (always valid against the shrinking hand, never the order the human clicked). The engine itself
  (`AIEngine`'s bottom loop, the discard loop) applies whatever order it is sent.

### The pin: the committing segment itself

`tools/play/linebuild.js` `sequenceTokens(seg)` writes the segment being committed, ONE token per
queued entry, in queue order: `<verb>=<name>` with the LineSpec verb the entry writes (`cast=`,
`act=`, `sacout=`, `vial=`, `retrace=`, `suspend=`, `channel=`, `eternalize=`, `equip=`, `blink=`,
`pod=`, ...), `land=` / `landtop=` for the drop, `le=` for ONE Land's Edge discard, and the manual
taps' own `tap=` tokens in place. The name is the plan ACTION's card (the line value, except
`pod=` / `ooze=` / `gyreturn=`, whose line value names a target -- the token names the source).
`index.html` `applyAccepted` sends it, marked `"**"`, for EVERY segment of two or more steps (a manual
tap is not a step) or with a tap or an untap demand to deliver. There is no multiset test left in the
viewer: nothing about the pin depends on the plan object, so the past-the-emit-cap case is the
ordinary case by construction. Same `S.castOrder` map, `co` step marker, `--cast-order` argument and
recorded `cast_order` as before.

`AIEngine::ReorderPlanCasts` hands a `"**"` list to `TurnSolver::ApplyHumanSequencePin` (pure, unit
tested):

* each step is matched to ONE unused plan action: the verb picks the KIND (`cast=` a hand cast, `vial=`
  a Vial put, `retrace=` a graveyard cast, `sacout=` a sac-for-mana / sac outlet, `act=` and every
  board verb an ability of a permanent), the name the card; failing that, any unused action of that
  card. A step that names nothing in the plan is dropped and counts no position.
* the matched actions are laid out FIRST, in queue order: `actions[0, human_seq_len)`.
* the land's step is `human_land_step` (the land is a step, so a tap's `position` counts it).
* each `le=` takes ONE unit of the plan's Land's Edge count, at its own position.
* **THE IMPLICIT-ACTION RULE (C).** An action of the matched plan that no step names goes where it is
  needed and never between two of the human's steps: a mana or suspend action before the first step
  (it is a source the line is paid from -- the pre-cast pass it always ran in); anything else after the
  last step (where the trailing pass always ran it). Land's Edge units no step placed run as one,
  after the last step.
* `MTG_HUMAN_LINE_ORDER=0` (the existing opt-out) degrades a `"**"` list to the historical cast-only pin.

`Plan` gains `human_sequence`, `human_land_step`, `human_seq_len` (in existing padding: the size pin
is unchanged; folded into the census key like the other human fields; absent from every plan
signature).

### The apply: `ApplyPlanDirect`, `human_v2`

Gated on `plan.human_sequence && human_action_order && searched_order && HumanPlayActive()` -- set only
by the viewer's side channel, so every search, rollout and autonomous apply is byte-identical.

* **The land.** Played first only when the human queued it first. Otherwise (or when a manual tap was
  queued ahead of it, or it is a Karoo, whose default is after the casts) the drop is RESERVED through
  the existing Karoo / searched-position deferral (`karoo_deferred`, so every breakpoint guard applies
  unchanged), the plan's own `land_slot` is not consulted, and the walk plays it right before its step
  (`play_deferred_land` / `play_deferred_karoo`, the same bodies). A step never reached falls back to
  the end-of-casts point, so the drop is never lost. A deferred shock land pays its 2 life only when a
  cast / retrace / dig follows it, or a manual tap names it.
* **The walk** (the explicit-order branch of `apply_plan_actions`) dispatches each step where it
  stands, through the body its canonical pass uses: a hand cast (sac-land ones too) through
  `apply_one`, its pre-pass convoke bodies tapped AT its step; a Vial put through `apply_vial`; a
  graveyard cast; ONE Land's Edge discard (`fire_lands_edge`, the post-cast loop's body, factored out);
  Krenko's `ApplyTapForTokens`; a declared `ApplySacForMana` / `ApplySuspend`; a land-search crack
  (`ApplyLandSearchActs` on that one action); a Garth activation (the ordered route's body); every
  other board activation through the trailing dispatcher (a one-action slice). Manual taps fire right
  before the step they were queued ahead of. Every step it applies is flagged (`human_v2_done`) and
  skipped by its canonical pass (the pre-cast pass, the sac-land / retrace / Land's Edge loops, the
  Krenko taps; the trailing pass is skipped as under `"*"`).
* **The prepay.** Declined (the line pays as it goes; the per-cast payer spends the float first) when
  the human declared a manual tap (D), or a step is itself a mana source the human placed (a declared
  sac-for-mana or mana-floating activation, a pre-pass convoke) -- the prepay would pay later casts
  before that source exists. Every other `"**"` line keeps the joint prepay (it allocates; it does not
  reorder anything the human said).
* **Server truth is unchanged:** a step the human put where it cannot be paid is dropped where it
  stands, never moved. (AIEngine's "an order that cannot pay for the line is not honoured" fallback is
  untouched and applies to `"**"` lines as it did to `"*"` ones.)
* `MTG_LINE_ORDER_TRACE=1` prints every step under its kind (`land`, `cast`, `sacland`, `retrace`,
  `landsedge`, `tokens`, `sacmana`, `suspend`, `landsearch`, `garth`, `activate`, `vial`); the scenario
  harness binds the same lines as data (`g_line_order_record`) for its `expect_line_order` assertion,
  and takes a per-line pin as `human_cast_orders`.

### Validation: `CheckLine`

`CheckLine` is now `CheckLineExact` (the old body) plus ONE retry: in human play, a line whose land is
positioned (`landat=` / `landafter=`) that grades legal_not_enumerated -- legal by every walk, but no
plan has that land position -- is matched against the plan with the same land played FIRST (same land,
same actions; the queue carries the position). The legality walks have already run, including the
prefix check that the casts queued before the land are payable WITHOUT it, which now covers
`landafter=` too: "cast Sol Ring, then the Mountain" with no other land is still Illegal, with its
reason. `linebuild.js` `landPosition` now encodes every land's queue position (it used to need the
`after` flag a landfall decision stamped).

### (E) Click order for the multi-pick decisions

`linebuild.js` `shrinkingIndices(picks)` re-bases click-ordered ORIGINAL hand indices onto the hand as
it stands at each successive decision (an earlier pick at a lower index shifts it down one);
`commitDiscardBatch` and `commitBottomBatch` send that instead of the descending sort. The same rule
found one more instance, fixed alongside: a multi-victim sacrifice burst (`commitBoardSel`) answered
in BOARD order; it now answers in the order the victims were clicked (the seeded default is the
heuristic pick first, then board order). Its frame-to-frame prediction (`expect`) does not depend on
the pick order.

### What a saved reference replays

Nothing recorded changes meaning. A `"*"` list, a bare cast list and a taps-only list take exactly the
branches they were recorded under (including the prepay, which the (D) guard leaves alone for them);
`land=` without a position is land-first; recorded discard / bottom ints are replayed as recorded.
Only a NEW game sends `"**"`.

### Tests

* `test/unit/test_human_sequence_pin.cpp` -- the pure half: land position, verb -> kind (the Augur in
  play vs the copy in hand), every queueable kind as a step, Land's Edge units, the implicit-action
  rule, an unmatched token counting no position, tap positions around the land, the eraser.
* Scenarios (`human_cast_orders` = the commit's pin, `expect_line_order` = the realised sequence):
  `burn_human_seq_land_after_cast` (A), `burn_human_seq_land_after_cast_validate` (A: `landat=1` on a
  deck with no positioned line accepts), `burn_human_seq_land_after_cast_validate_illegal` (A: an
  unpayable prefix stays Illegal), `burn_human_seq_sac_land_cast_first`,
  `treasure_hunt_human_seq_lands_edge_split`, `treasure_hunt_human_seq_retrace_first`,
  `goblins_human_seq_krenko_before_cast`, `dragonstorm_human_seq_suspend_after_cast`,
  `landfall_human_seq_landsearch_before_cast`, `melira_human_seq_convoke_at_its_step` (B; the convoke
  one is an OUTCOME difference too: at its step the just-cast Llanowar Elves is a free body, one Kitchen
  Finks stays up and attacks, 20 -> 17, where the pre-pass tapped both Finks), `burn_human_seq_implicit_sac_mana`
  (C: an implicit Dwarven Ruins sacrifice runs up front, the queued casts keep their order),
  `stompy_human_pretap_survives_prepay` (D; FAILED before the guard).
* `test/viewer_linebuild_check.js` -- the land position on a deck with no positioned line,
  `sequenceTokens` for every entry kind, `shrinkingIndices`; `test/viewer_client_check.js` -- the pin
  bookkeeping now expects the `"**"` lists.

### Measurements (binary 299fde38b's tree)

* Smoke (`bash test/regression.sh --smoke`): 136/136 PASS, **configs changed: 0**, 0 slower / 0
  faster / 0 play-changed at every depth -- the autonomous engine is byte-identical (every change is
  gated on a plan field only the viewer's side channel sets). Scenarios 247/247, unit 1719/1719.
* Reference replay (`viewer_protocol_check.py --strict`, 640 references): 222 ok, 418 repaired,
  0 play-drift / 0 shuffle-dead / 0 board-diverged / 0 enum-gap / 0 mull-drift / 0 contract-fail --
  the same tally as before the change (every saved game carries an old pin form).
* `viewer_linebuild_check.js`: 0 FAIL (3683 reference lines still rebuild). `viewer_client_check.js`:
  identical outcomes to the unchanged tree, i.e. every check passes except the two `equip` checks that
  fail on the unchanged tree too (KittyEquipment seed 2 / 6 no longer reach the boards they drive).
  Also failing identically on the unchanged tree, so not this change: `manual_tap_check.py`'s
  `MTG_ENERGY_C_MODE=0` control arm and `pay_line_color_check.py`'s two `=0` control arms;
  `human_line_order_check.py` SKIPs (its turn-6 line now grades legal_not_enumerated on both).
* **After the review fixes (tree 1bf6453d9):** smoke 136/136 PASS, **configs changed: 0**, 0 slower /
  0 faster / 0 play-changed; scenarios 254/254; unit 1726/1726; reference replay (640, `--strict`):
  222 ok / 418 repaired / 0 play-drift / 0 shuffle-dead / 0 board-diverged / 0 enum-gap / 0 mull-drift /
  0 contract-fail; `viewer_linebuild_check.js` 0 FAIL; `viewer_client_check.js` unchanged from the
  unchanged tree (only the two pre-existing `equip` failures).
* **Known limit of the validation preview:** its trial apply answers resolution-time choices (a
  sacrifice victim, a bounce, a tutor pick) with the engine default, as the fallback's trials always
  have; a later step whose payability depends on the human answering such a choice differently could
  be graded on the default's board.

### Left as is, and why

* **Out of scope by instruction** (asked of the user separately): AIEngine's cast-order swap when the
  human's order cannot pay ("AN ORDER THAT CANNOT PAY FOR THE LINE IS NOT HONOURED");
  `DeferHumanAuraSwapToCombat` (it now erases through `EraseHumanSequenceAction`, so the land's step and
  the taps' positions stay on the steps they named).
* **No user input exists today**, so there is no order to honour: trigger ordering, attacker choice,
  scry-bottom order, end-step auto-taps, the viewer's auto-answers.
* **DigDraw** is not a step: the viewer commits a cycle / sac-dig as a line of its own (`fireDig`
  clears the queue), so it is never sequenced against anything; it keeps its pass.
* **Validation granularity** -- CLOSED by the review fixes below: a pinned line validates WITH its
  exact sequence (`seq=`), so every step kind is checked in the order it will be committed.

### Review fixes (adversarial review of 7c550297b..99638a647, same day)

* **(1) A Land's Edge discard queued before the land burned the land being played.** The unit picked
  its land by `LandsEdgePitchOrder`, ignoring the land named in `le=<land>`; `PlayLandByName` then failed
  silently and the next cast was dropped on a validated line (reviewer repro `le6_2`; without the manual
  tap, `le5`, the "your order cannot pay" fallback threw the whole queue away and described only the
  casts). Each `le=` step now carries its land (`Action::discard_name`) and `fire_lands_edge` discards
  exactly that land (a named land not in hand is reported, nothing discarded); a land that cannot be
  played at its step -- or at the end-of-casts fallback point -- is reported. Scenarios
  `treasure_hunt_human_seq_le_names_its_land`, `treasure_hunt_human_seq_le_before_land_no_tap`.
* **(2) A deferred shock land ignored later PAID ACTIVATIONS.** The pay-2-life rule counted only casts /
  retraces / digs after the land, so `Kor Duelist, Temple Garden, equip Bonesplitter` left the Garden
  tapped and the Duelist hit for 1 instead of 6. It counts every mana-spending step after the land
  (`HumanStepSpendsMana`: any action with a mana cost), and a `"**"` land-first line uses the same
  widened rule (a superset of the old one, so never worse). Scenarios
  `kitty_human_seq_shock_land_pays_for_later_equip` + `kitty_human_seq_shock_land_first_control`
  (both: 20 -> 14, Garden untapped at 18 life).
* **(3) Validation could accept an order the commit would drop.** `landPosition` counts only `cast=` /
  `act=` entries, so a land queued after an equip encoded land-first and validated, and the walk then ran
  the equip before the land and dropped it silently. The fix is to validate the ORDER ITSELF: the viewer
  validates each pinned segment with `LB.validationLine` (its line plus `seq=<the "**" steps>`), and
  `src/main.cpp ValidateHumanSequence` previews the commit through the commit's own body
  (`PrepareHumanCommit`: the pin, the fallback, a trial apply of what will really be applied). A step
  the plan cannot match or could mean two plays, a dropped cast, or a step that does not happen where it
  was queued makes the line **Illegal with every reason**; a line the user-requested fallback rescues
  stays accepted. (`landat=<k>` keeps its documented meaning -- the cast count CheckLine maps onto a
  landfall slot -- because `seq=` now carries the exact all-kinds order and the preview checks it.)
  At the commit, every `"**"` step is watched: a non-cast step that leaves the state unchanged (the
  engine's dedup key plus zone sizes, the float and the life totals) is reported in the history as
  `⚠ <step> was NOT applied: ...` (`report_step_fail`); casts keep their `dropped_casts` report.
  Scenarios `kitty_human_seq_equip_before_land_rejected` (validation) and
  `kitty_human_seq_equip_before_land_reported` (commit).
* **(4) Same-name ambiguity.** A step now carries what the viewer knows about WHICH play it is --
  `#<src>` (the clicked permanent, from the thumb's `data-num`, stamped only when an enumerated action
  names that copy; the Equipment), `@<target>` (the equip host, the blinked creature, the equip-all host),
  `*<count>` (a blink's count), `+kicked` / `+adventure` -- and `ApplyHumanSequencePin` matches on all
  of it. The old "any unused action of that card" fallback is gone. A step that still matches two
  candidates that are not the same play (`HumanStepSameIdentity`: same action fields and
  indistinguishable permanents) is refused and reported, never picked. Unit tests.
* **(5) Convoke double tap.** An unmatched queued step is an error (reported; the validation rejects
  the line), not an implicit action; and the walk taps pre-pass convoke bodies only for a QUEUED step
  (an implicit one was tapped by the pre-cast pass). Unit test.
* **(a) Segment order.** `segmentParts` lifted extra land drops ahead of the line and pushed a repeated
  activation behind the whole segment; it now cuts a new segment exactly where an entry cannot join the
  current one (a second land, a repeat of a repeatable source, a deferred entry) and keeps every entry in
  queue order (`checkSegmentOrder`).
* **(b) Which land a cost discards.** Land's Edge: the named land (above). Retrace and a sac-land cast
  (Shard Volley) already ask the human (`g_play_retrace_chooser`, `PerformSacrificeLandCost`'s human
  chooser), so there is nothing to change.
* **(c) The fallback reports exactly what it changed.** Its existence and trigger are untouched (shared
  now as `PrepareHumanCommit`, byte-identical for the old pin forms). For a `"**"` line it names the
  queued order, what could not be paid in it, and the order actually played instead -- read off the
  trial applies through `g_line_order_capture`, for which every step of the canonical route is now
  noted too. Scenario `burn_human_seq_fallback_reports_what_changed`: "your order (cast Sol Ring, land
  Mountain) cannot pay for every spell in the line (not paid in that order: Sol Ring), so the WHOLE line
  was played in the engine's order instead: land Mountain, cast Sol Ring, which pays them all".

## DEFERRED: a dropped board ACTIVATION is silent in the viewer

*(2026-10-22: closed for `"**"` lines -- each step is dispatched alone, so "the state did not change" IS a failed step and is reported; see "Review fixes" above. What follows still describes the `"*"` route and the autonomous trailing pass.)*

Server-truth resolution says a declared action that cannot be executed is **dropped and reported**.
The drop half holds for activations and always has: every branch of the trailing dispatcher checks
its precondition and its payment *first* and no-ops without mutating anything ("stranded-outlet
safe"). The **report** half does not: `g_play_dropped_cast_sink` is fed from `apply_one` only, so
the viewer's `dropped_casts` channel covers hand casts and nothing else.

This gap predates the order work — the trailing pass could already strand an activation behind a
cast that spent the mana — but honouring the human's order makes it easier to reach on purpose,
since the human can now place an activation before the mana that would have paid for it.

**Why it is not fixed here, and what a fix needs.** The cheap proxies do not work:

* *"count `TapForCostDirect` failures around the action"* — `ApplyBlinkLoop` and
  `SpendRepeatActivations` **terminate on a failed payment by design** (the blink loop is
  self-funding and runs until it cannot pay), so a perfectly successful multi-activation would
  report as a drop.
* *"diff a cheap state fingerprint"* — misses the kinds whose only effect is a flag or a counter
  (`AnimateLand`'s `is_animated`, `JitteModeAbility`'s counters).

An exact fix needs a success flag returned from each of the twenty branches in
`apply_trailing_activations` (and its `AIEngine` twin), pushed into `g_play_dropped_cast_sink` on
failure. That is mechanical but touches every activation kind and wants its own reference sweep,
so it is its own change. Shipping a heuristic version would put false red "dropped" banners in
front of the player, which is worse than the current silence.
