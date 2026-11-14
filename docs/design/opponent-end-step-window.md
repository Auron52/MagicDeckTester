# Opponent end-step action window (BUILT for Incandescent Soulstoke, 2026-11-14; open items for the user below)

Status: **built and validated 2026-11-14 (see §0; local commits on branch `redelem-analysis`, not yet pushed)**; the original 2026-10-18 deferral text follows. Written 2026-10-18 by the LandfallElementals `lfe-fury` card cluster while
implementing Incandescent Soulstoke. The engine today activates instant-speed abilities only inside
our own main phases. This doc covers what a window at the end of the opponent's turn would add, why it
matters, how it could be built soundly, and what it would cost. (Superseded by §0: the
window is built. Before it, Soulstoke's put was a main-phase-only activation, disclosed as a PROVISIONAL partial in
its `cards.json` note.)


## 0. Status (2026-11-14, Red Elementals agent F) -- BUILT, the morph-flip shape

Soulstoke's put at the end of the OPPONENT's turn is built as a **searched choice made where the search crosses
the turn boundary**, reusing morph's opponent-end-step window rather than inventing a second one (the §3 sketch
predates that window; the morph flip, `docs/design/analysis-Morph.md` INT-2b/2c, is the precedent followed):

* **Shape.** `EotFaceUpMenu` (SpellEffects.h) now crosses every flip entry with `EotPutOptions` -- "put nothing"
  or a multiset of the distinct Elemental creature card NAMES in hand, one put per live source (sources are
  interchangeable there: every one untaps at our untap step), a put Nova Chaser crossed with its searched champion
  picks. One rank of that menu is the pin: `Plan::eot_face_up_choice` -> `GameState::scripted_eot_face_up`, the
  same field, fold sites, dedup keys and census as the flip. No new Plan/State field.
* **Coverage: every plan variant.** The BoundaryFan's face-up digit fans at every in-window crossing, so
  breakpoint / scry / discard continuations reach it exactly as base plans do (the §4 "base plans only" option
  was NOT taken). `FaceUpUncoveredPending` is now live for a live put source too; the hold pass
  (`FaceUpHoldPassMissing`), `SecondMainUnproductive`'s exemption and the executor's crossing node
  (`ChooseRealCrossingFaceUp`) follow it. Census: `MTG_FACEUP_AXIS_STATS` (searched-frame must read 0).
* **Step order.** Unchanged and shared: both `opponent_turn` lambdas (`GameEngine::RunTurnFrom`,
  `SimulateEndAndStartNextTurn`) run the self-sacrifice sweep, then (Soulstoke decks) the delayed-sacrifice sweep,
  then the window, then day/night. After a put, the window applies the opponent's cleanup (CR 514.2) to the
  until-end-of-turn state (the put's haste, a Reef land's landfall pump); floating mana is saved/emptied/restored
  around the window as for the flips (it pays from untapped sources only).
* **The delayed sacrifice** stays a bool: the window follows the opponent's end-step sweeps, so the next sweep of
  `sac_at_end_step` is our end step -- "the next end step". (A turn stamp would only matter for a window BEFORE
  that sweep.) An opponent extra turn is covered by the new opponent-end-step delayed-sacrifice sweep.
* **Holding mana.** The flip precedent: whatever the turn's plan leaves untapped is what the window can spend; the
  plan enumeration offers the lines that spend less (subsets, the hold pass). No reservation axis was added.
* **Holding the SOURCE (new, not in §3).** Soulstoke must be untapped at the window, and the goldfish attack policy
  swings with everything, so as first built the put was unreachable on every turn Soulstoke could attack. The HOLD is
  searched beside the natural attack in EVERY host that decides a pre-combat main (agent F2, the completeness rule):
  FSLineWin inside its combat branch (kind 4 of the searched attack/hold, `DorkAtkContestedKind` /
  `EotTapHoldSource`); every other host (SolveWithLookahead's candidate passes, breakpoint and group waves,
  escalation; the rollout's searched turns, which run through it; the opt-in reshuffle-averaged host) as a PLAN
  TWIN with `Plan::atk_dork_release = 4` right after its base (TurnSolver.cpp `AppendEotHoldTwins`), its combat
  under override 4 (`EotHoldCombatScope`). Gated on a usable source in the pre-apply state (`EotHoldTwinGate`); a
  twin whose post-apply hold no longer differs from the natural attack is skipped unscored (`EotHoldContested`).
  The hold never takes a tie. Executor: the existing `atk_dork_release` pin. Not carried (structurally): the
  this-turn kill checks (`LethalCutKillIsReal`, `WinlessSeedApplyWins`, the verify re-solve) -- holding only removes
  damage, so it cannot create a this-turn kill -- and the diagnostic hosts (`EnumerateEarliestWins`, the fd-pred
  dump).
* **The put creature's own enter choices** are menu variants too (agent F2, the no-greedy rule), the same two pins
  a main-phase put carries: `EotPutStep::tutor` (Flamekin Harbinger: the provider's front, every other rank up to
  `TutorSearchWidth`, the optional decline -- `Plan::tutor_choice`'s values) and `EotPutStep::look_top` (Risen
  Reef's land {default, flip} per look as the full product while 2^n <= 16, one-hot past it; a top-tutor
  entrant's CR 603.3b order {0..r} -- `Plan::look_top_pins`' layout), installed around that step's put only. Rank 0
  of every step is the unpinned front, so the playout's front entry is unchanged. Census (printed with
  `MTG_FACEUP_AXIS_STATS`): `eot-put enter choices: fanned steps=N unfanned=M` (M must read 0).
* **Human play / viewer:** the `eot_put` decision (modal card grid + "Put nothing"), after `face_up`.
* **Validation (agent F3, 2026-11-14).** Two defects were found and fixed. (a) A hold twin's post-apply state
  equals its base's, so the post-apply state dedups (the lookahead's axis-variant skip, `LaWaveDup` in the
  breakpoint wave, the group wave, and the opt-in `MTG_CAND_DEDUP` key) discarded it as a duplicate. The fix,
  `EotHoldFoldKey`, folds the kind-4 pin into those keys. (b) The look-top ORDER rows now put the unpinned default
  first, so rank 0 of every step really is "no pins". The full unit suite and `test/scenarios.sh` (533/533,
  including `redelem_soulstoke_opp_eot_firecat_leadership`) pass. The paired A/B at d3 b100 (Red Elementals
  840 games, Landfall Elementals 720) and the census are recorded in `docs/design/analysis-Red Elementals.md`,
  section "Soulstoke opponent end step". Morph, Goblins and Spirits are byte-identical to the pre-change
  binary.

**Still open for the USER (not settled by the agent):**
1. FSLineWin runs ONE alternative combat per plan, so where a dork kind (1-3) fires at the same node the Soulstoke
   hold is not searched there (counted: `fslinewin-shadowed-by-dork-kind` in the census line). Red Elementals has no
   mana dork; Landfall Elementals' Smokebraider could shadow it.
2. Mana holding follows the flip precedent (no reservation axis): a payer that taps the wrong colour in main 2
   can strand the {R}. Not observed in mono-red; LFE's three-colour mana could hit it.
3. The base provider's front (`EotPutOrder`: largest power first) decides ties and the playout; no deck provider
   orders or prunes the menu yet.

## 1. What

A decision point after the goldfish opponent's (empty) turn reaches its end step and before our next
untap. During it we may activate instant-speed abilities and cast instants or flash spells with
whatever is still untapped.

The goldfish opponent has no turn of its own in the engine. `GameEngine::RunTurnFrom` runs our turn
and ends with cleanup, the opponent's notional draw (`opponentdeck::EndOfTurnDraw`) and the day/night
check. The next call starts our next turn with `UntapStep`. The window would sit in that gap.

## 2. Why it matters (Incandescent Soulstoke first)

"{1}{R}, {T}: You may put an Elemental creature card from your hand onto the battlefield. That creature
gains haste until end of turn. Sacrifice it at the beginning of the next end step."

Real play activates it at the end of the **opponent's** turn:

* **The put creature survives our whole next turn.** "The next end step" is then OUR end step, so the
  creature attacks on our turn. It has been under our control since that turn began, so it needs no
  haste. It is sacrificed only at our end step.
* **The mana is free.** It is mana left untapped after our own turn. Those lands untap at our untap step
  anyway.
* **Soulstoke untaps** at our untap step and can activate AGAIN in our main phase for a second hasty
  creature. That is **two puts per turn cycle instead of one**. A put Fury is a 4/4 double striker under
  the lord, so the cycle hits for 16 instead of 8.
* **Enter triggers come a turn early.** Risen Reef's land enters tapped on their turn and untaps at our
  untap step: one extra untapped land on our turn. Mossborn Hydra's counter doubling happens a turn
  early.
* **Rules constraint.** Soulstoke must have been under our control continuously since our most recent
  turn began (summoning sickness, CR 302.6). One cast on our turn N can't activate at the end of the
  opponent's turn N, but it can from turn N+1 on.

The same window would serve other cards the repo already discloses as collapsed to the main phase:
* Aether Vial's instant-speed put (`analysis-White Knights.md`, `knights-copy-count-screen.md`).
* Flash creatures (the "disclosed Flash gap", `DecisionProviders.cpp`).
* Remand's end-step/opponent-turn windows (`remand-self-target.md`).
* Enlightened Tutor's upkeep window (`upkeep-instant-tutor-window.md`, the closest precedent).

**Effect of deferring:** the deck is UNDER-rated wherever the end-step put is the better line. That is
most turns once Soulstoke has survived a turn with an Elemental in hand.

## 3. Design sketch (the sound shape)

Recommended shape: **a plan axis that rides the state** (the `Plan::vial_charge_choice` /
`discard_choice` precedent). It is not a new decision node.

1. **Pin.** Add `Plan::opp_eot_choice`: none, or (source m_number, put card NAME). The main-phase plan
   that ends a turn carries it. `ApplyPlanDirect` copies it to a `GameState::scripted_opp_eot`, the
   `scripted_*` pattern.
2. **One shared window helper** in SpellEffects, `ApplyOpponentEndStepWindow(state)`. It reads the pin,
   re-checks legality (source untapped and not summoning-sick for {T}, card still in hand, cost
   payable), pays under the creature-ability scope, and calls the shared `ApplyPutFromHand`. A
   stranded pin is a no-op. One important difference from a main-phase put: "the next end step" is
   then the NEXT turn's, so the delayed sacrifice must be armed for the following end step, not swept
   at the one just passed. That needs a turn stamp on `sac_at_end_step` rather than a bool.
3. **Both worlds, same position.** Call it after cleanup, the opponent's draw and day/night, and before
   `++turn_number`:
   * real game: a new step at the end of `GameEngine::RunTurnFrom`;
   * rollout: `SimulateEndAndStartNextTurn`, between the `opponent_decked` early-out and the turn
     increment.

   Every one of the ~16 lookahead call sites goes through the latter, so search and rollout pick it up
   automatically. After the window: clear until-end-of-turn state again (the put's haste) and empty
   floating mana.
4. **Fan-out (searched).** `AppendSubdecisionAxes` adds pin variants for each (eligible source × distinct
   Elemental creature card name in hand). The variant's dedup key (`BuildDedupKeyBeforeLandSearchPins`)
   and the dominance fold must carry the pin, or the variants collapse. Beyond the horizon the playout
   needs a pin policy. Under the no-greedy rule that is allowed only past the horizon. A provider hook
   could order or restrict the candidates.
5. **Executor.** The real game's committed line pins the choice like `m_vial_choice_pin`
   (AIEngine.cpp). A human-play chooser (`opp_end_step`, its own decision type) emits only when an
   activation is actually affordable, the echo/vial precedent.
6. **Viewer.** New decision type plus the four wiring sites (`tools/play/DECISIONS.md`).

## 4. Hazards (why this is not a one-session job)

* **Step-order parity.** End step, cleanup, opponent draw, day/night, decked early-out, the window, a
  second clear of until-EOT state and floating mana, then the next untap. This order must be identical
  in `RunTurnFrom` and `SimulateEndAndStartNextTurn`, or every game drifts (fd-diverge).
* **Coverage of the fan-out (needs a user ruling).** The Vial axis fans out over BASE plans only.
  Breakpoint, scry and discard continuations never carry its pin. Copying that for this window would
  make "end-step put after a breakpoint line" unfindable at any budget, which the completeness rule
  forbids. Fanning out over every plan variant multiplies the plan count. One has to be chosen, and the
  choice is the user's.
* **Holding mana.** The payer has no reservation for an end-step activation. A plan that wants the
  window must leave {1}{R} untapped through combat and the second main. The search can price that only
  if the pin's plans are enumerated with the hold. That is either a mana-reservation axis or a
  "hold N" plan variant, the deferred `mana-source-reservation.md` problem.
* **A kill inside the window.** Soulstoke's put cannot kill there (no attack, and Fury's ETB has no
  player target). A generic window (a burn instant) could. The rollout's loop head plays on from a
  lethal state, so it needs an early-out that does not advance the turn.
* **Turn bookkeeping.** During the window `turn_number` and `ActivePlayer()` still mean "our turn N",
  and nothing marks "it is the opponent's turn". Any card reading "your turn" / "this turn" needs care.
* **Budget and replay.** The axis multiplies the variant count, so the anytime budget predictor moves.
  It is a new `--choices` decision: references saved before it shift their positional indices unless
  the window is emitted only when a pin is affordable (the echo rule), and the reference replay checker
  must learn the type.

## 5. Cost estimate

Medium to large:
* ~15–25 code sites: Plan/State pin, ApplyPlanDirect, both turn loops, AppendSubdecisionAxes, dedup and
  dominance keys, AIEngine pin plumbing, provider default, the claude-play emitter, the GUI branch,
  `DECISIONS.md` and the auditor manifest;
* unit tests and scenarios, and a held-out A/B on the decks it moves;
* 2–3 focused days;
* user rulings on fan-out coverage and on holding mana.

Not deck-specific: once built, Vial, Flash and Remand could use it too.

## 6. Open decisions for the user

1. Build the window now (the deck is under-rated until then), or ship LandfallElementals with the
   main-phase-only Soulstoke disclosed as PROVISIONAL?
2. If building: fan-out coverage — base plans only (fast, lossy at any budget) or every variant
   (complete, costlier)?
3. If building: is holding mana for the window a plan axis (searched) or a reservation rule?
