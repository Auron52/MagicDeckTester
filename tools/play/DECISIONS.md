# Play-viewer decision registry

The authoritative list of every interactive decision the play viewer (`tools/play`) can
surface, and the four wiring sites each one lives at. It is the human half of the
`cards.json`-param → decision-type manifest that `scripts/audit_viewer_decisions.py`
enforces mechanically. (That auditor also runs an **oracle-text cross-check** — scanning
each card's real text for choice phrases like "any target" / "sacrifice a creature" /
"choose one" and flagging any the params don't model — which is the source-of-truth check
for *what* needs a decision here in the first place; this table is *how* to wire it.)

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
| `replicate` | `g_play_replicate_chooser` (`ReplicateChooser`) | `ApplyPlan` `apply_one` replicate loop | `WriteReplicateDecisionJson` | `replicatePanelHtml` | modal |
| `land_entry` | `g_play_land_entry_chooser` (`LandEntryChooser`) | `TurnSolver::PlayLandByName` (shared land drop) | `WriteLandEntryDecisionJson` | `landEntryPanelHtml` | modal |
| `vial_charge` | `AIEngine::SetExternalVialChooser` | Vial upkeep charge | `WriteVialDecisionJson` | `promptPanelHtml` | board |

**Soulfire Eruption / Crackle with Power full-board targeting** does NOT use a distinct type:
the `g_play_soulfire_chooser` (`SoulfireTargetChooser`) lambda in `main.cpp` **reuses the generic
`target` decision** (via `EnumerateTargetSets`, tagged `random_damage`, `source` = the spell name)
— so at runtime these surface as a `target` decision with a full subset enumeration, not as a
`soulfire_targets` type. `WriteSoulfireDecisionJson` exists but is **dead code (never called)**;
treat `target` (source-matched) as the decision to verify. *(Confirmed by live trace 2026-07-08:
Soulfire cast → `target`, source="Soulfire Eruption", 256 = 2⁸ subset options.)* The plan-variant
own-target **count** (`soulfire_own_targets`) still rides the `main_phase` plan list separately.

Plan-variant sub-decisions ride the `main_phase` plan list rather than their own `type`
(the human picks a plan index): `tutor_target`, `fetch_target`, `chosen_x`, `ponder_keep`,
`soulfire_own_targets` (count), `enchant_target` (which creature an Aura enchants — Bogles).
For these the only per-deck work is confirming **every** legal option appears as a distinct plan
variant (human-play runs unpruned). `enchant_target` is emitted directly by `CollectActions`
(one variant per `LegalEnchantTargets`, no provider narrowing) and labeled in `SummarizePlan`
("Rancor → Kor Spiritdancer") + the per-action JSON (`enchant_target` + `enchant_target_name`),
so distinct placements are human-distinguishable. NB the human-play `plan_signature` dedup
(`TurnSolver::EnumeratePlans`) must key on `enchant_target` alongside `tutor_target`/`chosen_x`/etc,
or plans differing only in the aura's target collapse to the first-enumerated creature — a
dead-decision bug the Auras claude-play sweep caught (2026-07-23) and fixed. The autonomous dedup
still keys on cast NAMES only (byte-identical GT), delegating the target to the heuristic there.

## Surfacing options (viewer "⚙ Options" menu)

The engine **always emits** every decision it can (the chooser is installed unconditionally in
`RunClaudePlay`); whether a modal is *shown* is a **viewer** concern. The play viewer's persistent
options menu (localStorage `mdt_surface`, `AUTO_RESOLVABLE` in `index.html`) lets the user set any
listed decision to **"let AI decide"**, in which case `advanceTo` auto-replies the decision's single
`heuristic_default` int without surfacing the modal (it behaves exactly like instantly clicking the
AI default, so undo/checkpoints stay 1:1). Only single-int decisions whose answer *is*
`heuristic_default` are auto-resolvable this way. **Default is surface-on for everything except
`land_entry`** (shock lands / Frostboil Snarl), which ships default-**off** because the choice is
repetitive and its heuristic (pay life / reveal only when it benefits you) is usually right. This is
a convenience toggle, never an engine-level skip — the audit gate still requires the decision be
fully wired (all four sites) regardless of the menu default.

## Known gaps (not yet a type — see analyze-deck Stage 6a)

- **Modal "choose one/two" (non-damage)** — e.g. Reality Spasm tap-vs-untap. Needs an
  engine-model change (de-abstract the untap float into literal targets), not viewer wiring.
- **Cascade / Retrace SEARCH target** — which card the flip casts. Heuristic-picked today;
  build a type only when a deck needs it.
- **Light-Paws fetch target** (Bogles/Auras) — which Aura the `aura_cast_tutor_attach` trigger
  tutors onto Light-Paws. Heuristic-picked (highest power contribution), like the cascade target;
  build a type only if a deck needs it surfaced.
