# Play-viewer decision registry

The authoritative list of every interactive decision the play viewer (`tools/play`) can
surface, and the four wiring sites each one lives at. It is the human half of the
`cards.json`-param → decision-type manifest that `scripts/audit_viewer_decisions.py`
enforces mechanically.

**Use this doc two ways:**

1. **Adding a new decision type (analyze-deck Stage 2c-ter, "bucket B").** A card whose
   choice matches no row below needs a *new* type. Replicate an existing row's four sites —
   don't invent a shape. Pick the GUI shape (modal art-grid vs board-click prompt) from a
   sibling row. Then add the card's param → type mapping to the auditor's manifest so the
   coverage gate covers it.
2. **Verifying an existing type fires (Stage 5h).** The auditor drives games and diffs the
   decision types that surfaced against the set the deck's card params predict. This doc is
   where you look when a type is missing to find which of the four sites is broken (the
   classic bug: chooser wired to the autonomous path, never the shared `ApplyPlan` path — so
   it compiles, is byte-identical, and never fires).

## The four wiring sites (uniform across every bucket-B type)

1. **Chooser hook** — `src/core/GameLogger.h`: a `using <Name>Chooser = std::function<…>`
   typedef + `extern thread_local <Name>Chooser* g_play_<name>_chooser;`. **Must be nulled in
   the `RevealLogPause` RAII** (ctor sets it to `nullptr`, dtor restores) so it is inert
   during search/rollout and fires only on real human resolution.
2. **Call site** — the *shared* resolution code the real game runs (`src/core/SpellEffects.h`
   / `src/core/EffectHandler.cpp` for resolution effects; `TurnSolver::ApplyPlan`'s `apply_one`
   for plan-execution mechanics like retrace/Land's Edge). **Gated on the pointer being
   non-null**, falling back to the heuristic when null. Wiring it to `AIEngine`'s autonomous
   `ExecutePlan` instead of the shared `ApplyPlan` is the recurring dead-chooser bug.
3. **Protocol emitter** — `src/main.cpp`: a `Write<Name>DecisionJson` printed between
   `<<<CLAUDE_DECISION>>>`/`<<<END_DECISION>>>` (exit 70), with a `"type"` string,
   enumerated option indices (reuses the integer `--choices` stream), and a
   `heuristic_default` field.
4. **GUI branch** — `tools/play/index.html`: dispatch on `S.decision.type`, routing into the
   central dialog or board clicks using card **images**, never text (the play-viewer decision
   principle — no choices in the history panel).

## Registry

GUI shape: **modal** = central art-grid dialog (`*PanelHtml`); **board** = board-click /
bottom prompt (`promptPanelHtml`). Line numbers are hints — anchor on the symbol name.

| `type` | Chooser hook (GameLogger.h) | Call site | Emitter (main.cpp) | GUI (index.html) | Shape |
|---|---|---|---|---|---|
| `main_phase` | — (plans, no chooser) | `TurnSolver::ApplyPlan` | `WriteDecisionJson` | `renderBoard` | board |
| `mulligan` | — (mulligan path) | KeepHand path | `WriteMulliganDecisionJson` | `mulliganPanelHtml` | modal |
| `bottom` | — (mulligan path) | bottoming path | `WriteBottomDecisionJson` | `bottomPanelHtml` | modal |
| `scry` / `surveil` / `reorder` | `g_play_top_chooser` (`TopChooser`) | `SpellEffects.h` look-at-top | `WriteTopDecisionJson` | `lookPanelHtml` | modal |
| `target` | `g_play_target_chooser` (`TargetChooser`) | `EffectHandler` damage/removal | `WriteTargetDecisionJson` | `promptPanelHtml` | board |
| `divide` | (target/divide path) | `EffectHandler` divided damage | `WriteDivideDecisionJson` | `promptPanelHtml` | board |
| `bounce` | `g_play_bounce_chooser` (`BounceChooser`) | `SpellEffects.h` ETB bounce | `WriteBounceDecisionJson` | `promptPanelHtml` | board |
| `sacrifice` | `g_play_sacrifice_chooser` (`BounceChooser`) | sac-land cost | `WriteBounceDecisionJson` | `promptPanelHtml` | board |
| `dig` | `g_play_dig_chooser` (`DigChooser`) | `SpellEffects.h` ETB dig | `WriteDigDecisionJson` | `digPanelHtml` | modal |
| `discard` | `g_play_discard_chooser` (`DiscardChooser`) | cleanup discard | `WriteDiscardDecisionJson` | `discardPanelHtml` | modal |
| `expressive_iteration` | `g_play_ei_chooser` (`EIChooser`) | Expressive Iteration resolution | `WriteEIDecisionJson` | `eiPanelHtml` | modal |
| `retrace_discard` | `g_play_retrace_chooser` (`RetraceDiscardChooser`) | `ApplyPlan` `apply_one` retrace | `WriteRetraceDiscardDecisionJson` | `retraceDiscardPanelHtml` | modal |
| `soulfire_targets` | `g_play_soulfire_chooser` (`SoulfireTargetChooser`) | Soulfire own-target resolution | `WriteSoulfireDecisionJson` | `promptPanelHtml` | board |
| `vial_charge` | `AIEngine::SetExternalVialChooser` | Vial upkeep charge | `WriteVialDecisionJson` | `promptPanelHtml` | board |

Plan-variant sub-decisions ride the `main_phase` plan list rather than their own `type`
(the human picks a plan index): `tutor_target`, `fetch_target`, `chosen_x`, `ponder_keep`,
`soulfire_own_targets` (count). For these the only per-deck work is confirming the provider's
`*Candidates` hook returns **every** legal option (human-play runs unpruned), so each legal
option appears as a distinct plan variant.

## Known gaps (not yet a type — see analyze-deck Stage 6a)

- **Modal "choose one/two" (non-damage)** — e.g. Reality Spasm tap-vs-untap. Needs an
  engine-model change (de-abstract the untap float into literal targets), not viewer wiring.
- **Cascade / Retrace SEARCH target** — which card the flip casts. Heuristic-picked today;
  build a type only when a deck needs it.
