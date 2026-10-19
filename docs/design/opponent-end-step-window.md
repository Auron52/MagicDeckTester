# Opponent end-step action window (DEFERRED — PROVISIONAL, awaiting user sign-off)

Status: **not built.** Written 2026-10-18 by the LandfallElementals `lfe-fury` card cluster while
implementing Incandescent Soulstoke. The engine today activates instant-speed abilities only inside
our own main phases. This doc covers what a window at the end of the opponent's turn would add, why it
matters, how it could be built soundly, and what it would cost. Until the user decides, Soulstoke's
put is a main-phase-only activation, disclosed as a PROVISIONAL partial in its `cards.json` note.

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
* Aether Vial's instant-speed put (`analysis-WhiteKnights.md`, `knights-copy-count-screen.md`).
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
