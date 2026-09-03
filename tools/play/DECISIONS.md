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
| `discard` | `g_play_discard_chooser` (`DiscardChooser`) | cleanup discard; **non-cleanup discards via `ChooseNonCleanupDiscardIndex`** (Burning-Fist Minotaur's `{1}{R}, Discard a card:` activation cost in `ApplyActivatePump`; Neheb, the Worthy's combat-damage trigger in `ResolveCombatDamage`) | `WriteDiscardDecisionJson` | `discardPanelHtml` | modal |
| `expressive_iteration` | `g_play_ei_chooser` (`EIChooser`) | Expressive Iteration resolution | `WriteEIDecisionJson` | `eiPanelHtml` | modal |
| `retrace_discard` | `g_play_retrace_chooser` (`RetraceDiscardChooser`) | `ApplyPlan` `apply_one` retrace | `WriteRetraceDiscardDecisionJson` | `retraceDiscardPanelHtml` | modal |
| `replicate` | `g_play_replicate_chooser` (`ReplicateChooser`) | `ApplyPlan` `apply_one` replicate loop — **FALLBACK ONLY** since 2026-08-26; the count is normally a plan variant (see below) | `WriteReplicateDecisionJson` | `replicatePanelHtml` | modal |
| `land_entry` | `g_play_land_entry_chooser` (`LandEntryChooser`) | `TurnSolver::PlayLandByName` (shared land drop) | `WriteLandEntryDecisionJson` | `landEntryPanelHtml` | modal |
| `dragon` | `g_play_dragon_chooser` (`DragonChooser`) | `PerformTutorToBattlefield` (SpellEffects.h, shared executor+rollout) | `WriteDragonDecisionJson` | `dragonPanelHtml` | modal |
| `sac_tutor` | `g_play_sac_tutor_chooser` (`SacTutorChooser`) | `PerformUpkeepSacTutor` (SpellEffects.h, shared executor+rollout) | `WriteSacTutorDecisionJson` | `sacTutorPanelHtml` | modal |
| `lightpaws` | `g_play_lightpaws_chooser` (`LightPawsChooser`) | `PerformLightPawsAttach` (SpellEffects.h, shared executor+rollout) | `WriteLightPawsDecisionJson` | `lightPawsPanelHtml` | modal |
| `vial_charge` | `AIEngine::SetExternalVialChooser` | Vial upkeep charge | `WriteVialDecisionJson` | `promptPanelHtml` | board |
| `firebreathe` | `g_play_firebreathe_chooser` (`FirebreatheChooser`) | `AIEngine::Firebreathe` (combat, `GameEngine.cpp:361`) | `WriteFirebreatheDecisionJson` | `firebreathePanelHtml` | modal |
| `lackey_put` | `g_play_lackey_chooser` (`LackeyChooser`) | `FireCombatDamageCheatIntoPlay` (SpellEffects.h, shared executor+rollout) | `WriteLackeyDecisionJson` | `lackeyPanelHtml` | modal |
| `free_cast` | `g_play_free_cast_chooser` (`FreeCastChooser`) | `AIEngine` post-combat main, before the plan menu (Maelstrom Archangel) **+ the trigger resolvers in `EffectHandler`** — `ResolveCascadeTrigger` (cascade "you may cast it"; decline bottoms), `ResolveEtbExileFreeCast` (Breaching Dragonstorm; decline → hand), `ResolveShuffleRevealFreecast` (Creative Technique; decline stays exiled). `source` on the chooser/JSON names the offering trigger. | `WriteFreeCastDecisionJson` | `freeCastPanelHtml` | modal |
| `demonstrate` | `g_play_demonstrate_chooser` (`DemonstrateChooser`) | `EffectHandler::ResolveDemonstrate` (Creative Technique's cast trigger; the copy resolves before the original) | `WriteDemonstrateDecisionJson` | `demonstratePanelHtml` | modal (yes/no, echo's shape) |
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

Planeswalker loyalty-ability TARGET reuse note (2026-09-03): Oko's +1 (`elk_transform`), Bolas's +3
(`destroy_own_noncreature`) and -2 (`steal_creature`) all reuse `target` via a dedicated
`g_play_loyalty_chooser` (`LoyaltyTargetChooser`, `GameLogger.h`) consulted from
`ApplyLoyaltyAbility` (`SpellEffects.h`); `WriteTargetDecisionJson` gets a `loyalty_desc` string
(what happens to the chosen permanent) that flags the JSON with a `"loyalty"` field so
`promptPanelHtml` words the prompt without a damage number and never assumes the opponent's face is
a legal target. Deliberately NOT a new type — `target` already owns board-click selection over an
arbitrary legal set (the Soulfire precedent above). The **enumerator's** candidate set for these
three effects stays search-narrow (own permanents, value-filtered) so autonomous play is unchanged,
but under `HumanPlayActive()` each effect's `CollectActions` gate is loosened to LEGALITY ONLY (a
human is entitled to a play the AI ranks badly — e.g. Elking your own tapped mana dork on turn 2),
and at resolution `human_target` (SpellEffects.h) always offers the ability's FULL rules-legal set —
either side of the board, tapped or not — regardless of which single candidate the plan enumerated.
This also fixed a routing bug: `CheckLine` used to fold a loyalty ability's target into the `enchant`
(attach) sub, which speaks Aura vocabulary ("leave it unattached") for a walker that isn't attaching
anything — loyalty targets are now excluded from that path and asked only via the `loyalty`
ability-choice sub + this `target` decision. See also `LoyaltyAbilityText`/`LoyaltyActionLabel`
(SpellEffects.h), the single source of truth for how an ability prints in the dialog, the plan menu
and the history label — those three used to disagree (Bolas's -2 had no English mapping at all).
Balan's attach-all, Stoneforge's put, the Equip itself and the Jitte -1/-1 / lifegain modes are
`main_phase` plan lines (`equip=` / `attachall=` / `sfput=` / `jittemode=` LineSpec verbs) — see
**Board activations** below for how a human reaches them.

**Replicate moved from a resolution dialog to a PLAN VARIANT (2026-08-26).** Its count is now
`Action::replicate_count`, fanned under human play as one cast variant per k with the cost priced at
`effective + k x printed`, and surfaced as a `replicate` `CheckLine` SubChoice (the splice pattern).
This is not cosmetic: a dialog asked at RESOLUTION is asked after the mana is already committed, so
the whole-turn payment had solved only the cast's own pips and could spend a coloured source on a
generic one, leaving the copies unaffordable — the play-tester's "it offered me `max_count: 0`" with
white sources untapped. Measured on slivers over 40 driven games: **12 games gain a copy the old path
never offered, 0 lose one, and 10 of the 12 went from a flat 0 to 1–2.** The dialog above is retained
as the FALLBACK for a cast no plan variant priced (a cast made inside a draw-breakpoint re-solve),
which is now 5 firings where there used to be 52. When a plan pinned the count, resolution obeys it
and does NOT re-prompt — the count is a decision already taken, and asking twice lets the second
answer contradict the mana the first one reserved. `MTG_REPLICATE_DIM=0` is the revert hatch.

**Board activations (an ability of a permanent ALREADY IN PLAY) — a FIFTH wiring site.**
These are not a decision `type` at all: they are part of the main-phase LINE, so the four sites above
do not apply and the registry cannot describe them. They need their own three things, and forgetting
any one makes the ability unusable by a human while every engine-side check stays green (this has now
happened to Call of the Wild, all three planeswalkers, Garth, and every Equipment in the deck built
around them):

> **THIS SITE IS NOW GATED (2026-08-25).** `scripts/audit_viewer_decisions.py` carries a
> `BOARD_ACTIVATIONS` manifest (param → the `activate` / `verb:<v>` marker its plan action must
> carry) and asserts, over the sweep, that the marker was actually OFFERED. It exists because the
> auditor used to classify every one of these params as *"rides main_phase"* — a claim it asserted
> and never checked — and four abilities shipped unreachable behind it: **Deathrite Shaman's**
> graveyard exile (a plan action with no `activate` flag), **Mutavault's** animate and **Sliver
> Hive's** token ability (no `Action::Kind` AT ALL — they were greedy mana sinks in
> `AnimateLandsShared` / `ActivateTapTokensShared`, so a human could neither ask for them nor decline
> them), and **Twinshot Sniper's** Channel (a from-HAND ability that serialised identically to
> casting the creature). An ability whose cost was never affordable while its source sat untapped is
> reported UNVERIFIED, not a miss — the manifest's third field is where that cost is read from.

1. **`activate: true` on the plan action** (`src/main.cpp`, the plan-action JSON). This is what makes
   the permanent's board thumb clickable — the GUI can only queue cards from HAND, so without the flag
   there is no way to express the ability. Add `activate_source` when the clicked permanent is not the
   action's own `card` (Stoneforge's put names the Equipment it puts, which is in hand).
2. **A LineSpec verb** the GUI writes (`verb` on the same action; absent = the ordinary `cast=`, which
   CheckLine matches inside its `orderNames` multiset). Give a kind its own verb whenever `cast=<name>`
   would be AMBIGUOUS with a hand cast of the same-named card — that is why `equip=` exists.
3. **A `SubChoice` in `TurnSolver::CheckLine`** for anything the verb does not encode. Every variant of
   one activation carries the same `card_name`, so with no sub they share a dedup signature, the first
   enumerated one wins, and the human silently gets it with no dialog. Which creature an Equip attaches
   to (`equip`), which loyalty ability (`loyalty`), which Jitte mode (`jitte`), how many times a
   repeatable ability fires (`activations`) all live here. Register the key's trailing relation word in
   `renderDimPick`'s `srcName` strip, or the dialog's header art 404s.

Anything decided AFTER the commit belongs in the choose-variant dialog (site 3); anything the verb must
NAME has to be picked at QUEUE time, which is what the GUI's activation picker (`activationPickerHtml`,
opened when one source offers several distinct activations) is for.

**The verbs added 2026-08-25** (all from the user's "abilities can't be used" report):

| verb | ability | why it needs its own verb |
|---|---|---|
| `gyexile=<mode>` | Deathrite Shaman's `{B},{T}: exile an instant/sorcery, each opponent loses 2` | the action names the SOURCE, so `cast=Deathrite Shaman` collides with hard-casting one of the other three copies |
| `gyreturn=<returned card>` | Haven of the Spirit Dragon's `{2},{T}, Sacrifice: return target Dragon creature card from your graveyard to your hand` | a land is played, not cast, so `cast=<land>` is meaningless — and unlike every other verb the value is the **returned card**, not the source: one Haven with three distinct Dragons in the graveyard is three different activations, and naming the land could not tell them apart |
| `animate=<land>` | Mutavault's `{1}: becomes a 2/2` | a land is played, not cast, so `cast=<land>` is meaningless; several copies must stay distinguishable |
| `taptoken=<land>` | Sliver Hive's `{5},{T}: create a Sliver` | same |
| `channel=<card>` | Twinshot Sniper's `{1}{R}, Discard this card: 2 damage` | a from-HAND ability, not a board activation and not a cast — `cast=<name>` cannot say which of the two ways you are playing the card |

`animate` and `taptoken` are enumerated **only under `HumanPlayActive()`** (the `jitte_modes_open`
precedent) and the greedy sinks `AnimateLandsShared` / `ActivateTapTokensShared` **stand down** there,
so the human's answer is the only one — while autonomous play keeps the measured-better greedy and no
ground truth moves. `bestow` is NOT a verb: a bestowed cast already splits from the creature cast by
its `enchant` sub, and it now also emits an explicit `bestow` mode sub so the choose dialog can say
which of the two it is (before, the mixed sub/no-sub set fell into the flat fallback picker and the
two modes rendered as the same card name twice).

**THE FLAT FALLBACK PICKER IS GONE (USER 2026-09-01).** *"I would actually like to kill every one of
those confusing and ugly dialogs with overlapping text and random pictures for all decklists. Keep in
mind that for real decisions that need to be made we want a dialog or targeting on the board, but we
never want one of these dialogs."* `renderChooseFlat` — the "Choose how to resolve" grid that dumped
every variant into one row of repeated card art labelled by its whole line summary in a nowrap badge
— is deleted. Several engine comments still say a missing sub "drops the dialog into the flat
fallback picker"; read those as *the dimension walk cannot separate the variants cleanly*, which is
still a reason to emit the sub. What changed is only what happens when one is missing:
`renderChooseDialog` no longer bails, because a variant that lacks a dimension simply has no value in
it and `choiceOf` already reports that as `'—'` (rendered with a per-kind label like *"don't equip
it"*). The walk now owns every shape, and a dimension whose choices are not all distinct card art is
rendered as a LIST rather than a grid — the grid is what overlapped. Nothing else about real
decisions changed: they still get this dialog or a board click.

**EQUIP is the exception: it is a DRAG, not a click (USER 2026-08-24).** *"Change the means to equip
equipment to resemble the approach used for auras — rather than activate them normally you would drag
them onto a creature."* An Equipment in play (attached or not) renders `draggable` + `data-equip` and
is dropped onto one of your creatures; `equipTargetsFor` reads the legal hosts straight out of the
enumerated plans (`equip_host` / `equip_host_name` on the Equip action) exactly as `enchantTargetsFor`
does for Auras, and the drop stamps the host on the queued entry's `target`/`targetName` — the SAME
fields a dragged Aura uses. Two things follow from reusing those fields rather than inventing new
ones: the queued equip renders stacked behind its host like a planned Aura, and dragging it again
re-aims it in place (`retargetPlanAura`, never a duplicate — an Equipment has ONE host).
`clickActivationOptions` filters `equip` out of the click path, so an equip-only Equipment has no
click affordance at all while a Jitte — Equip plus two counter modes — still opens the picker for the
modes and is dragged for the attach.

**The host rides on the LINE, not on a sub-decision (2026-09-01).** `equip=` now writes
`equip=<name>[#<equipment m_number>][@<host m_number>]`, and the drag stamps both. It used to write a
bare name and leave the host to the `equip` sub-decision, which the viewer then auto-resolved from
the drag. That worked only while every host had a distinct NAME: the sub's `choice` string IS the
host's name, `CheckLine` dedups plan variants by a signature built from those strings, so with **two
Kor Duelists** in play all four (Wargear host, Greaves host) pairings shared a signature and three
were silently dropped before the viewer ever saw them — *"it doesn't allow me to equip Grafted Wargear
followed by Lightning Greaves to the second Kor Duelist; instead it ends up equipping the Lightning
Greaves to the first"* (KittyEquipment seed 6). No client-side shortcut can recover a variant the
engine already collapsed, which is why the fix is in the line. Consequences, all of them wanted:
a stamped line matches exactly ONE plan, so equipping is an **accept** with no dialog at all; the
source number distinguishes two copies of one Equipment (moving the attached Bonesplitter is not the
same play as attaching the loose one); and `EquipsMatch` treats a 0 as a wildcard, so an unstamped
legacy line still fans out as before. Belt-and-braces on the engine side: `SubChoiceHostLabel`
appends `#1`/`#2` to a same-named host in every board-object sub (`equip`, `enchant`, `sacrifice`,
the Jitte's `-1/-1` victim), so *no* dimension can silently collapse two creatures into one; and
`SubChoice.num` carries the m_number to the client, so the `attachPrefFor` auto-resolve matches by
identity rather than by a display string two creatures can share. Pinned by
`test/scenarios/kitty_equip_two_same_named_hosts.json` (four variants must survive) — asserting only
`choose` would still pass if two of the four vanished.

**An Equip is NEVER part of the `cast=` multiset (2026-09-01).** `CheckLine` used to fall back to
matching an Equip action by its card name inside `spec.casts` whenever the line declared no `equip=`.
Under that rule `cast=Bonesplitter` — *cast the copy in my hand* — also matched the plans that only
**attach** the Bonesplitter already in play, two plays with nothing in common beyond a name. The user
got a variant dialog on a plain equipment cast and, picking wrong, would have left the card in hand
(KittyEquipment seed 7 T3). Equip is now matched against `spec.equips` in **both** directions: an
empty `spec.equips` means *this line performs no equip*, so a plan that equips is not a match for it.
Pinned by `test/scenarios/kitty_cast_equipment_is_not_an_equip.json`.

**Equipping can also be decided FROM HAND (USER 2026-09-01).** *"Ideally we would allow equipping to
be decided from hand or by dragging the equipment on the field. That would mean two operations,
playing it plus equipping."* Dropping a hand Equipment on one of your creatures queues **both**
operations — the cast and the equip — and they encode as one line,
`cast=<name>;equip=<name>#<src>@<host>`, which matches the single plan the engine has always
enumerated for the pair (`land=Plains; cast: Bonesplitter, equip Bonesplitter → Kor Duelist`). That
plan was previously unreachable from the GUI: the only equip gesture was dragging a permanent that
does not exist until the cast resolves. `handEquipTargetsFor` gates the affordance on a plan that
really casts the card, so the drop is never a silent no-op; `equipSrc`/`equip_src` supplies the
m_number even from hand (numbers are stable across zones). The pair renders as ONE thumb on the host
— see the duplication note below.

**A queued equip is drawn ONCE (2026-09-01).** *"When equipping, the equipment remains in two places,
in the plan and separately on the field."* The queued equip has always rendered stacked on its
intended host, but the Equipment ALSO kept its own battlefield thumb (or its stack under its current
host, when being re-hosted), so the same card appeared twice on the field. `queuedEquipSrcNums()`
suppresses the resolved copy by m_number while its equip is queued, and the from-hand pair suppresses
the planned CAST thumb by the same rule. The HAND thumb deliberately stays visible with its queued
count badge — that is a different zone, not a duplicate.

**Umezawa's Jitte's +2/+2 is a main-phase activation like its other two modes (USER 2026-08-24).**
*"Pump for Umezawa's Jitte should be handled like the others. I should be able to activate them any
time counters are present."* It used to be reachable only inside combat (`JitteDamageMath`'s greedy
spend plus the pre-strike `firebreathe` prompt). It is now `JitteModeAbility` mode **3**, gated on
charge counters being present AND the Jitte being ATTACHED — "equipped creature" is not a target, so
an unattached activation is a guaranteed no-op and a plan menu must never contain one. Because
`ActivationFamilyKey` buckets every Jitte activation of one source into a single odometer group (one
per plan), spending SEVERAL counters is a repeat COUNT on `chosen_x` (the Call of the Wild pattern)
and surfaces as the `activations` sub, not as N actions. Both routes coexist: the main phase spends
what the human asks for, combat spends whatever is left.

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

**AN AURA IS NEVER OFFERED "unattached" (USER 2026-09-01, EldraziDisplacerFlicker).** *"Auras cannot
be unattached. That option should not be offered by dialogs."* CR 303.4 — an Aura enters attached to
the target chosen as it was cast; only an **Equipment** can sit loose, which is why `equip`'s missing
value still reads *"don't equip it"* and the attack-dig `attach_host` prompt still offers **Leave
unattached** (that prompt is gated on `params.is_equipment` in `FireAttackDigAttach`, so an Aura
cannot reach it). The offending option came from the choose dialog's dimension walk: a variant with
no `enchant` sub reports `'—'`, which `renderChooseDialog` labelled *"leave it unattached"*.
**CheckLine drops that sub whenever it cannot NAME the host** — its lookup scans battlefield
*creatures* and then the hand, so an "Enchant land" aura (Wild Growth / Fertile Ground / Overgrowth /
Trace of Abundance) attached to a land **already in play** carries no sub, while the same aura
attached to the land being **played this turn** does (the hand fallback finds it). The viewer now
recovers the host from the enumerated plan's own `enchant_target_name` before the walk, so that
dialog asks *"Aether Hub or Conservatory?"*; `'—'` for an `enchant` dimension can now only mean a
plan past the emitted-plan cap and reads *"another host"*.

> **STILL OPEN (engine, not the viewer):** because the sub is what the `CheckLine` dedup signature is
> built from, TWO land hosts that are both already in play share an EMPTY signature and **collapse**,
> so the human is never asked which one — the same dead-decision shape as the `plan_signature` bug
> above, one layer down. Reproduce: EldraziDisplacerFlicker seed 1, T2+, `cast=Wild Growth` returns a
> single variant with `subs: []` while `d.plans` lists a distinct plan per legal land. The fix is in
> the `enchant` `addSub` branch of `TurnSolver::CheckLine` (accept a non-creature host — resolve the
> art/label off any battlefield permanent, not `IsCreature()` only), and no client-side shortcut can
> recover a variant the engine has already collapsed.

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

**A hand card's PLAYABLE FACES are a wiring site too** (added 2026-08-24 after a user report). An MDFC
whose front is a nonland and whose back is a land — Turntimber Symbiosis // Turntimber, Serpentine
Wood — reaches the palette with the FRONT's `kind` (`nonpermanent`), so double-click and drag both
cast the sorcery and the land drop the engine happily enumerates as `land=<FRONT name>` had no
affordance at all. The card was unplayable as a land by hand (rejection artifact:
`logs/play/rejections/StompySurprise_cod_s5_gi4_t2.json`, where `cast=Turntimber Symbiosis` was
rejected for `{4}{G}{G}{G}` while `land=Turntimber Symbiosis;cast=Priest of Titania` was accepted).
Fixed by emitting `mdfc_land_back` on the hand card and rendering a `▣ land` badge, gated on the
enumerated plans actually offering that land drop (`landPlayableNames`) so it is never a no-op. A
land//land MDFC (Branchloft Pathway) is unaffected — its front IS a land, so the ordinary land route
reaches it and the `face` sub picks the side. **The general rule: whenever a card can be played more
than one way, check that every way has a route in the palette — `kind` describes only one of them.**
