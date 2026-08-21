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
| `reorder` (no-shuffle variant) | same chooser, `LookKind::ReorderNoShuffle` (Mirri's Guile upkeep: arrange top 3, NO shuffle option offered) | `SpellEffects.h` `ReorderTopNoShuffle` | `WriteTopDecisionJson` | `lookPanelHtml` | modal |
| `target` | `g_play_target_chooser` (`TargetChooser`) | `EffectHandler` damage/removal | `WriteTargetDecisionJson` | `promptPanelHtml` | board |
| `divide` | (target/divide path) | `EffectHandler` divided damage | `WriteDivideDecisionJson` | `promptPanelHtml` | board |
| `bounce` | `g_play_bounce_chooser` (`BounceChooser`) | `SpellEffects.h` ETB bounce | `WriteBounceDecisionJson` | `promptPanelHtml` | board |
| `sacrifice` | `g_play_sacrifice_chooser` (`BounceChooser`) | sac-land cost **+ creature-sac outlets** (`ChooseSacOutletVictimIndex`, SpellEffects.h) | `WriteBounceDecisionJson` | `promptPanelHtml` | board |
| `dig` | `g_play_dig_chooser` (`DigChooser`) | `SpellEffects.h` ETB dig | `WriteDigDecisionJson` | `digPanelHtml` | modal |
| `discard` | `g_play_discard_chooser` (`DiscardChooser`) | cleanup discard | `WriteDiscardDecisionJson` | `discardPanelHtml` | modal |
| `expressive_iteration` | `g_play_ei_chooser` (`EIChooser`) | Expressive Iteration resolution | `WriteEIDecisionJson` | `eiPanelHtml` | modal |
| `retrace_discard` | `g_play_retrace_chooser` (`RetraceDiscardChooser`) | `ApplyPlan` `apply_one` retrace | `WriteRetraceDiscardDecisionJson` | `retraceDiscardPanelHtml` | modal |
| `replicate` | `g_play_replicate_chooser` (`ReplicateChooser`) | `ApplyPlan` `apply_one` replicate loop | `WriteReplicateDecisionJson` | `replicatePanelHtml` | modal |
| `land_entry` | `g_play_land_entry_chooser` (`LandEntryChooser`) | `TurnSolver::PlayLandByName` (shared land drop) | `WriteLandEntryDecisionJson` | `landEntryPanelHtml` | modal |
| `dragon` | `g_play_dragon_chooser` (`DragonChooser`) | `PerformTutorToBattlefield` (SpellEffects.h, shared executor+rollout) | `WriteDragonDecisionJson` | `dragonPanelHtml` | modal |
| `sac_tutor` | `g_play_sac_tutor_chooser` (`SacTutorChooser`) | `PerformUpkeepSacTutor` (SpellEffects.h, shared executor+rollout) | `WriteSacTutorDecisionJson` | `sacTutorPanelHtml` | modal |
| `lightpaws` | `g_play_lightpaws_chooser` (`LightPawsChooser`) | `PerformLightPawsAttach` (SpellEffects.h, shared executor+rollout) | `WriteLightPawsDecisionJson` | `lightPawsPanelHtml` | modal |
| `vial_charge` | `AIEngine::SetExternalVialChooser` | Vial upkeep charge | `WriteVialDecisionJson` | `promptPanelHtml` | board |
| `firebreathe` | `g_play_firebreathe_chooser` (`FirebreatheChooser`) | `AIEngine::Firebreathe` (combat, `GameEngine.cpp:361`) | `WriteFirebreatheDecisionJson` | `firebreathePanelHtml` | modal |
| `lackey_put` | `g_play_lackey_chooser` (`LackeyChooser`) | `FireCombatDamageCheatIntoPlay` (SpellEffects.h, shared executor+rollout) | `WriteLackeyDecisionJson` | `lackeyPanelHtml` | modal |
| `free_cast` | `g_play_free_cast_chooser` (`FreeCastChooser`) | `AIEngine` post-combat main, before the plan menu | `WriteFreeCastDecisionJson` | `freeCastPanelHtml` | modal |
| `attach_host` | `g_play_attach_host_chooser` (`BounceChooser` shape) | `FireAttackDigAttach` (SpellEffects.cpp, shared executor+rollout combat) | `WriteAttachHostDecisionJson` | `promptPanelHtml` (attach_host case) | board |
| `jitte` | `g_play_jitte_chooser` (`FirebreatheChooser` shape) | `ResolveCombatDamage` (Combat.cpp, shared executor+rollout) | `WriteJitteDecisionJson` | `jittePanelHtml` | modal (turn-keyed `--jitte` side-channel, like `firebreathe`) |

KittyEquipment reuse notes (2026-08-13): the Armored Skyhunter attack-dig's *put* pick reuses the
`dig` type (`FireAttackDigAttach` → `g_play_dig_chooser`, source = the Skyhunter); only the attach
host needed the new `attach_host` type above.

Turntimber Symbiosis reuse note (2026-08-21): the `look_top_put_creature_count` put also reuses the
`dig` type (`PerformLookTopPutCreature` → `g_play_dig_chooser`, source = `"<card> (put)"` — the
marker keys the onto-the-battlefield wording in both the emitter note and `digPanelHtml`). Under
human play the plan menu enumerates ONE empty-target cast (no clairvoyant named variants — they
leaked the top-7 into a no-reveal game and went stale the moment a same-line Worldly Tutor
reordered the library); the human picks at resolution off the REAL look, or -1 to put nothing.
Autonomous play keeps the searched named-variant axis unchanged. Unexpectedly Absent's target reuses `target` (the
tuck branch consults `g_play_target_chooser` with every nonland permanent legal, own side included).
Balan's attach-all, Stoneforge's put, and the Jitte -1/-1 / lifegain modes are `main_phase` plan
lines (`attachall=` / `sfput=` / `jittemode=` LineSpec verbs).

**Firebreathe amount (#4) — the first COMBAT-phase decision, and the first on a KEYED SIDE-CHANNEL.**
At combat, leftover mana is spent greedily on attacker pumps (`ApplyFirebreathing`). The human instead
picks how many pump ACTIVATIONS to buy (0..max, default = greedy max), so they can hold mana back.
Because it fires at combat (attackers + leftover mana unknown at main-phase commit) it CANNOT ride the
main-phase plan and must not shift the positional `--choices` stream, so it rides a **turn-keyed
side-channel** `--firebreathe "turn:count,..."` (firebreathing fires once per combat = once per turn).
`--firebreathe-prompt` makes the engine emit the decision (exit 70) for any unanswered combat turn; the
viewer always passes it. **Existing references (no `--firebreathe`) replay byte-identically as greedy —
the payoff of the decision-indexed side-channel design** (`docs/design/decision-indexed-choice-protocol.md`).
The client records the amount in `S.firebreathe` (a `{turn: count}` map, sent in `cfg()`), NOT `S.choices`;
`commitFirebreathe` pushes a ZERO-int `S.steps` entry carrying the turn key (`fb`) so undo (`rollbackStep`)
pops it and drops the side-channel entry, keeping the #2 checkpoint/step bookkeeping 1:1. Chooser nulled
in `RevealLogPause` (search/rollout greedy) and installed only when there's a recorded answer or live
prompting → autonomous + reference-check runs are byte-identical.

**Dragonstorm `dragon` put override:** the human picks WHICH library Dragons enter (Dragonstorm's
tutor-to-battlefield), up to `max_puts` (the storm count); the engine keeps the rule's fixed play
ORDER (`DragonstormProvider::TutorToBattlefieldPutOrder` — Lathliss → Scourges → Utvara → haste). The
reply is ONE int per candidate (1 = put this copy), read positionally like `divide` / Soulfire, so any
subset is expressible; `heuristic_subset` / `ai_set` = the rule's default (pre-checked). The
`tutor_to_battlefield` param maps to `dragon` in the auditor manifest (was `main_phase` while the
selection was search-only). The SELECTION is the human's; the ORDER stays the rule's.

**Defense of the Heart `sac_tutor` upkeep put:** at the controller's upkeep with the opponent on
3+ creatures, the enchantment sacrifices itself and puts up to two library creature cards onto the
battlefield. The human picks WHICH creature copies (same reply shape as `dragon`: one 0/1 flag per
candidate, read positionally; candidates enter in ascending candidate order). `ai_set` = the
provider's `SacTutorPutList` default (closed-form immediate-drain maximisation — token-makers enter
before a Massacre Wurm so its sweep catches the fresh tokens). "Up to two": an empty selection is
legal. The `upkeep_sac_tutor_creatures` param maps to `sac_tutor` in the auditor manifest.

**Light-Paws `lightpaws` tutor-attach:** when an Aura you CAST resolves, Light-Paws, Emperor's Voice
searches your library for an Aura (MV ≤ the cast Aura's, a name you don't already control, whose own
enchant restriction Light-Paws satisfies) and puts it onto the battlefield attached to itself. The human
picks WHICH Aura — the panel shows the whole library Aura pool (a search reveals your library) with the
fetchable copies clickable and the rest LOCKED, plus a "Fetch nothing" (it is a *may* search). Reply = a
pool index, or `-1` to decline; `heuristic_default` = the engine's highest-static-power eligible Aura.
Each Aura you cast in a turn triggers its own fetch → its own decision. Autonomously byte-identical (the
chooser is nulled in every search/rollout by `RevealLogPause`; `aura_cast_tutor_attach` maps to
`lightpaws` in the auditor manifest, was a heuristic-picked known gap while the fetch was engine-only).

**Creature-sac outlets (Skirk Prospector / Siege-Gang / Pashalik) — the ACTIVATION is a line verb,
the VICTIM reuses `sacrifice`.** Two halves, deliberately split (2026-08-08, viewer issues #1/#2/#4/#8):

- *How many activations* rides the line as the repeatable **`sacout=<outlet name>`** verb
  (`LineSpec::sac_outlets`, `ParseLineSpec`, matched in `CheckLine`), one token per creature
  sacrificed. It needs its own verb because these are neither hand casts nor a pass — Skirk's
  "Sacrifice a Goblin: Add {R}" used to be an *implicit* mana source the enumerator added only when
  a cast needed it (`planSacs`), so a human could not ask for one, and a sac-only line read as
  "cast nothing" at `CheckLine` stage 0. **An empty `sac_outlets` keeps legacy matching**
  (`SacForMana` implicit, `SacCreatureOutlet` via `cast=<name>`), which is what keeps saved
  references validating unchanged. In the GUI a sac outlet is a board click like Krenko's tap; the
  queued entry is `{kind:'activate', sacout:true}` so every existing planbar/badge renderer works,
  and only `encodeLine` branches.
- *Which creature dies* is answered at RESOLUTION by the existing **`sacrifice`** board decision, so
  the human picks against the real board (a burst of k prompts k times, as each sac resolves).
  `ChooseSacOutletVictimIndex` addresses victims by BATTLEFIELD INDEX, not card number — tokens all
  carry `m_number == 0`, so the card-number path the heuristic uses cannot distinguish two tokens.
  Returns -1 with no chooser installed → the autonomous `CanonicalSacVictim` path is byte-identical.

Also: `DeferSacOutletPreCombat` (a measured-neutral *perf* prune that hides the value outlets from
the pre-combat main) is skipped under `HumanPlayActive()` — to a human the outlet just vanished from
main 1 and reappeared in main 2.

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
`soulfire_own_targets` (count), `enchant_target` (which creature an Aura enchants — Bogles),
`land_face` (which face of a modal double-faced Pathway land to play — Branchloft {G} vs
Boulderloft {W}).
For these the only per-deck work is confirming **every** legal option appears as a distinct plan
variant (human-play runs unpruned). `enchant_target` is emitted directly by `CollectActions`
(one variant per `LegalEnchantTargets`, no provider narrowing) and labeled in `SummarizePlan`
("Rancor → Kor Spiritdancer") + the per-action JSON (`enchant_target` + `enchant_target_name`),
so distinct placements are human-distinguishable. NB the human-play `plan_signature` dedup
(`TurnSolver::EnumeratePlans`) must key on `enchant_target` alongside `tutor_target`/`chosen_x`/etc,
or plans differing only in the aura's target collapse to the first-enumerated creature — a
dead-decision bug the Auras claude-play sweep caught (2026-07-23) and fixed. The autonomous dedup
still keys on cast NAMES only (byte-identical GT), delegating the target to the heuristic there.
`land_face` follows the same pattern: `EnumeratePlansWithLand` emits one land-play variant per face
(both carry the front hand-card `land_to_play`, so both survive `CheckLine`'s land-name match and
surface as a `face` choose sub); the DB synthesizes the back face (`mdfc_back_name`/`mdfc_back_produces`)
and `PlayLandByName`/`TryPlaySpecificLand` enter the chosen face's identity in lockstep (fd-diverge 0).
The human-play `plan_signature` keys on `|face=` so the two faces don't collapse; the autonomous dedup
stays cast-name-only (this IS a modeling change, so autonomous GT shifts — Pathway now commits to one
colour instead of a dual).

**Archangel free cast (`free_cast`) — a one-time TRIGGER, not a menu option.** Maelstrom Archangel's
"whenever this creature deals combat damage to a player, you may cast a spell from your hand without
paying its mana cost" is modelled as a banked charge (`GameState::free_casts_available`) spent in the
post-combat main: against a passive opponent nothing happens between the combat damage step and the
second main, so the timing is equivalent. The charge was originally spent through `#FREE` **plan
variants** in the ordinary main-phase menu, and that was wrong in two ways the player hit directly:
a one-time trigger became a standing option castable at any point in the phase, and because the paid
and free variants of one card carried the same `CheckLine` signature the dedup kept the PAID one, so
simply queuing the card silently threw the charge away. It is now asked ONCE, before the plan menu,
with a decline option ("may") -- exactly the shape of the Lackey put above. The chosen spell is applied
through the normal commit path (`ApplyPlan` on the enumerated single-action free plan), and the bank is
zeroed either way, so no `#FREE` variant ever reaches the plan menu in human play. The AUTONOMOUS search
still uses the plan-variant path (the chooser is null there, and nulled by `RevealLogPause` in every
rollout), so ground truth is unaffected.

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
  <!-- Light-Paws fetch target is now the wired `lightpaws` type (see Registry), no longer a gap. -->
