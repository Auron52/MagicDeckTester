# Damage-reflection pipeline (Stuffy Doll deck) — engine design

Status: DESIGN, 2026-11-17. Nothing here is implemented yet. The deck ledger is
`docs/design/analysis-Stuffy Doll.md`, and its STANDING DECISIONS bind every step below.
Authoritative card text comes from the Scryfall JSONs and Scryfall rulings, which were fetched on
2026-11-17 into `build/scratch/stuffy/scry/` and `build/scratch/stuffy/rulings/` on the build
volume. Line numbers below are hints that will drift; anchor on the symbol names.

Every one of the deck's eleven new or changed cards hangs off one missing engine concept:
**a creature being dealt damage is an event that triggers things**. Today the engine has no such
event. Our creatures are almost never damaged, and where they are, about fifteen different inline
sites mark damage and kill creatures each in their own way (§1.1). This document designs that event
once, as one shared function used by both the executor and the rollout. It then hangs the cards,
their search decisions, the rollout policy, the viewer, and the provider off that function.

---

## 0. The cards, verbatim, and what each needs from the engine

| Card | Cost | Oracle (Scryfall) | Needs |
|---|---|---|---|
| Stuffy Doll | `{5}` | Artifact Creature — Construct 0/1. Indestructible. As this creature enters, choose a player. Whenever this creature is dealt damage, it deals that much damage to the chosen player. {T}: This creature deals 1 damage to itself. | chosen-player ETB, reflection trigger (fixed recipient), {T} self-ping |
| Nomads en-Kor | `{W}` | Creature — Kor Nomad Soldier 1/1. {0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control instead. | 1-point redirection shields (replacement effect) |
| Shaman en-Kor | `{1}{W}` | Creature — Kor Cleric Shaman 1/2. Same {0} ability. {1}{W}: The next time a source of your choice would deal damage to target creature this turn, that damage is dealt to this creature instead. | shields + source-keyed pull shield |
| Mogg Maniac | `{1}{R}` | Creature — Goblin 1/1. Whenever this creature is dealt damage, it deals that much damage to target opponent or planeswalker. | reflection (opponent only) |
| Boros Reckoner | `{R/W}{R/W}{R/W}` | Creature — Minotaur Wizard 3/3. Whenever this creature is dealt damage, it deals that much damage to any target. {R/W}: This creature gains first strike until end of turn. | reflection (any target, itself included) |
| Screaming Nemesis | `{2}{R}` | Creature — Spirit 3/3. Haste. Whenever this creature is dealt damage, it deals that much damage to any other target. If a player is dealt damage this way, they can't gain life for the rest of the game. | reflection (any OTHER target) + per-player life lock |
| Spiteful Sliver | `{2}{R}` | Creature — Sliver 2/2. Sliver creatures you control have "Whenever this creature is dealt damage, it deals that much damage to target player or planeswalker." | a GRANTED reflection whose instance count = number of Spiteful Slivers |
| Blasphemous Act | `{8}{R}` | Sorcery. This spell costs {1} less to cast for each creature on the battlefield. Blasphemous Act deals 13 damage to each creature. | board-count cost reducer (both sides), 13-damage sweep through the pipeline |
| Chain Reaction | `{2}{R}{R}` | Sorcery. Chain Reaction deals X damage to each creature, where X is the number of creatures on the battlefield. | X counted at resolution, sweep through the pipeline |
| Guilty Conscience | `{W}` | Enchantment — Aura. Enchant creature. Whenever enchanted creature deals damage, this Aura deals that much damage to that creature. | "host deals damage" trigger, on every damage event including combat; loop termination |
| Loran's Escape | `{W}` | Instant. Target artifact or creature gains hexproof and indestructible until end of turn. Scry 1. | trick payload: temp indestructible (+ inert hexproof), cast scry |
| Forbidden Orchard (re-review) | — | Land. {T}: Add one mana of any color. Whenever you tap this land for mana, target opponent creates a 1/1 colorless Spirit creature token. | timing review (§12) |

The rulings that drive the design:

* **Reckoner, Nemesis and Spiteful Sliver:** "If [it] is dealt damage by multiple sources at once
  ..., its ability triggers once and one target is dealt that much damage." So triggers fire **once
  per creature per damage event**, for the total.
* **Lethal damage still triggers:** "[It]'s ability will trigger even if it is dealt lethal damage"
  (Reckoner, Nemesis, Sliver). A reflector killed by the same event still deals its damage, using
  last-known information (LKI).
* **Stuffy Doll:** "if Stuffy Doll is dealt 3 damage, its triggered ability deals 3 damage, not 1",
  and "If your life total is brought to 0 or less at the same time that Stuffy Doll is dealt damage,
  you lose the game before its triggered ability goes on the stack." So the state-based actions
  (SBA) run before any reflection trigger is put on the stack.
* **Blasphemous Act:** "The total cost to cast Blasphemous Act is locked in before you pay that
  cost" (a creature sacrificed for mana while paying does not raise the price); the discount "can't
  reduce the total cost to cast the spell below {R}".
* **Chain Reaction:** "The value of X is determined as Chain Reaction resolves."
* **Nomads / Shaman en-Kor:**
  * "It can redirect damage to itself."
  * "It is possible to redirect more damage to a creature than that creature's toughness."
  * "You can use this ability as much as you want prior to damage being dealt."
  * "When you redirect combat damage it is still combat damage."
* **Screaming Nemesis:** the life lock is per player, lasts the rest of the game, and stays even
  after Nemesis leaves the battlefield.
* **Damage from a reflection trigger is not combat damage** (Reckoner, Sliver), even when combat
  damage caused the trigger.

---

## 1. Survey: what exists today

### 1.1 Damage to creatures

* **The field.** `Permanent::damage` (`src/core/Permanent.h:756`). It resets in
  `GameEngine::CleanupStep` (`GameEngine.cpp:1543`), in the rollout's `SimulateEndAndStartNextTurn`
  (`TurnSolver.cpp:79097`) and in `EotWindowEndOfTurnReset` (`SpellEffects.h:45699`).
* **Lethality.** `LethalToughness(p, state)` (`SpellEffects.h:28053`) is the shared toughness that
  includes lords, CDAs and equipment. `CounterPingSbaDead` (`SpellEffects.h:35635`) is the
  executor-identical death test: toughness ≤ 0 kills even through indestructible, and lethal damage
  kills unless the creature is indestructible.
* **Indestructible.** `Keyword::Indestructible`, plus the turn-scoped `Permanent::temp_indestructible`
  (`Permanent.h:524`), both read through `Permanent::HasKeyword` (`Permanent.h:895`).
  `temp_indestructible` is set only by Boros Charm mode 2 (`SpellEffects.h:24391-24397`). It is
  cleared at `GameEngine.cpp:1551`, `TurnSolver.cpp:79105` and `SpellEffects.h:45706`. There is
  **no temporary hexproof** anywhere: `Keyword::Hexproof` is a printed, inert tag (`Card.h:52-56`).
* **Executor SBA.** `GameEngine::CheckStateBasedActions` (`GameEngine.cpp:1645-1730`):
  * The dead card goes to the CONTROLLER's graveyard (1710) and Equipment detaches (1719-1725).
  * `OnCreatureDies` runs after the erase loop (1726-1730).
  * It does **not** sweep orphaned Auras.
  * It is called after every stack resolution (1638) and after combat damage (1386).
* **Rollout SBA.** The rollout never runs that pass (`SpellEffects.h:35667`, "the rollout runs no
  SBA pass"). It relies on inline kills plus checkpoints: `DrainPendingZeroToughness`,
  `SweepOrphanedAuras` (`SpellEffects.h:29255`, run only at combat pre-declare and turn start in the
  rollout: `TurnSolver.cpp:78540`, `79211`), and `LegendRuleSbaCheckpoint`.
* **Death cascade.** `OnCreatureDies(state, dead_controller, dead_card, was_token, minus_counters,
  dwatch, total_counters)` (`SpellEffects.h:15009`) runs synchronously in both worlds. Nothing goes
  on the stack.
* **Every site that marks creature damage** (from the survey; S = shared helper, E = executor only,
  R = rollout only):

  | Site | Kind | Kill handling |
  |---|---|---|
  | `PerformDamageAllCreatures` `SpellEffects.h:18975` (Pyroclasm) | S | two-pass, `LethalToughness`, indestructible respected, **owner's** graveyard, detaches Equipment AND clears `aura_attached_to`, `OnCreatureDies` + `FireOppCreatureDies` |
  | `PerformDamageEachCreatureAndPlayer` `:20247` (Rolling Earthquake / Pyrohemia) | S | the above + `dmgev::DealDamageEvent` |
  | `PerformMvCastDamageOppCreatures` `:12746`, `etb_damage_each_opponent` `:13924` | S | bare `EffectiveToughness`, owner's graveyard, no `OnCreatureDies` |
  | `FireOwnEtbTriggersBody` fight `:13300` | S | via `g_pending_zero_toughness` → `DrainPendingZeroToughness` `:35670` |
  | `DealPermanentAbilityDamage` `:17163` | S | `LethalToughness` + `CounterPingKillAt` |
  | `ResolveEtbDividedCreatureDamage` `:20035` (Fury) | S | two-pass, clears Aura links, `OnCreatureDies` |
  | `FlushIncineratorDamageTriggers` `:6574` | S | pre-trigger SBA, then `CounterPingKillAt` |
  | `FlushKeranosTriggers` `:30024` | S | **no creature kill** (life SBA only) — dies only at the executor's SBA |
  | `SoulfireDig` `:32578`, `CrackleHitExtraTargets` / `CrackleApplyTargets` `:42636/42683` | S | `EffectiveToughness`, **no indestructible check**, no `OnCreatureDies` |
  | `ResolveCounterPing` `:36488` | S | `CounterPingSbaDead` → `CounterPingKillAt` |
  | `EffectHandler::ResolveDirectDamage` `EffectHandler.cpp:915` | E | none inline; the executor SBA kills |
  | `ApplyPlanDirect` human any-target branch `TurnSolver.cpp:73041-73062` | R | `LethalToughness`, **no indestructible**, owner's graveyard, no `OnCreatureDies` |
  | `ApplyPlanDirect` creature burn `TurnSolver.cpp:73148-73158` | R | bare `EffectiveToughness`, **no indestructible**, erased with **no graveyard push and no `OnCreatureDies`** |

  These disagree on four things: the kill predicate, the graveyard (owner or controller), whether
  `OnCreatureDies` runs, and whether an Aura link is cleared. The two rollout-only rows are real
  executor/rollout divergences that no comment records. None of these is on this deck's paths, so
  this design does not fix them (§11, risk R7), but **none of them may be reused**.

### 1.2 Sweepers

* **Pyroclasm.** `damage_all_creatures` (`CardDatabase.h:831`) → `PerformDamageAllCreatures`.
  Dispatched from the executor at `EffectHandler.cpp:756-760` and from the rollout's `apply_one`
  twin at `TurnSolver.cpp:73677-73684`.
* **Rolling Earthquake.** `x_damage_each_creature_and_player` → `PerformDamageEachCreatureAndPlayer`
  (`EffectHandler.cpp:763-767` / `TurnSolver.cpp:73686-73690`). Its X is a searched axis
  (`GenericProvider::XCandidates`, `DecisionProviders.cpp:7306-7345`).
* **Blasphemous Act** is `damage_all_creatures: 13` plus a cost reducer that does not exist yet.
  **Chain Reaction** needs a new "X = creature count at resolution" read.

### 1.3 Triggers and damage events

* **No "is dealt damage" trigger exists anywhere.** Boros Reckoner's entry (cards.json, the
  `vanilla_creature` at ~6254) bracket-notes its trigger as inert, because "nothing in this sim ever
  deals damage to our creatures". Apex Altisaur's Enrage (~9049) is a no-op.
* **The real stack.** `StackEntry` (`GameState.h:145`) with `TriggerKind` (`GameState.h:250`:
  None, Cascade, EtbExileFreeCast, Demonstrate, ExtraTurn, CastExileTargets, CastDestroyTarget,
  Ripple, CastTeamPump). These are all cast or enter triggers. Damage and death triggers resolve
  inline through ad-hoc queues in both worlds: `pending_incinerator_damage`, the Niv/Curiosity
  `RunDrawPingStack` (an explicit LIFO, `SpellEffects.h:29933`), and `FlushKeranosTriggers`.
* **`src/core/DamageEvents.h`** (`dmgev::`) models damage to **players** only:
  * `DealDamageEvent` (`:267`): one event from one source, with Purity prevention and Tamanoa
    triggers.
  * `SbaCheckpoint` (`:147`): our own death, parked at `kLostLife`.
  * The won-lock `WonLocked` (`:136`).
  * It is gated on `GameState::dmg_events_armed`, which `GoldFishRunner::StampDeckTraits` sets
    (`GoldFishRunner.cpp:1978-1992`). That gate is the pattern this design copies.
* **"Creature deals damage to an opponent."**
  * The hook is `OnCreatureDealtDamageToOpponent` (`SpellEffects.h:30129`), used for Curiosity and
    Tandem Lookout, gated on `deck_has_draw_ping`.
  * Combat fires it per connecting creature (`Combat.cpp:152-167`, events at `:592`), from the
    shared `ResolveCombatDamage` (`Combat.cpp:438`; first-strike variant at `:224`).
  * Combat calls it on both sides: executor `GameEngine.cpp:1357`, rollout `TurnSolver.cpp:78771`.
  * Combat face damage is applied at `Combat.cpp:359` and `:582` (`players[opp].life -= power`).
  * **Combat never damages a creature:** the goldfish opponent never blocks.
* **"Any target" damage triggers are all folded to the opponent's face:**
  * `etb_damage_any` (`SpellEffects.h:13866-13875`)
  * `draw_trigger_damage` (`:29947`)
  * `attack_trigger_damage_any` (`:24608`)
  * `dies_trigger_damage` (`:15163`)

  Two **searched** trigger-target precedents exist:
  * **Keranos** (`FlushKeranosTriggers`, `SpellEffects.h:30024`). Its legal set is the opponent's
    face, every legal permanent, and our own face (`ChosenTarget{kind, index}`). The target is
    pinned by rank through `ScriptedKeranosTargets` (a cursor plus a first-offer probe) or by
    `state.scripted_boundary_keranos`. The viewer goes through `g_play_target_chooser`.
  * **Chandra's Incinerator** (`FlushIncineratorDamageTriggers`):
    * SBA runs before the triggers.
    * The provider supplies the candidates (`IncineratorTargetCandidates`, `DecisionProvider.h:1364`).
    * `g_incinerator_pin_cursor` with `g_incinerator_pins` holds one pin per trigger ordinal.
    * The probe-and-fan lives in `AppendSubdecisionAxes` (`TurnSolver.cpp:98298-98353`): apply the
      plan on a copy, read the legal list at the next unpinned ordinal, and clone the plan once per
      option.
    * The pins ride `Plan::incinerator_targets` (`TurnSolver.h:1782`) and the during-cast
      `ContPins` (`TurnSolver.h:30-67`, `ContPinScope.h`).

### 1.4 Loops

* **Walking Ballista + Heliod.** `RunPingLoop` (`SpellEffects.h:36622`): an execution-verified loop
  that stops when an iteration fails to restore its premise, with a hard cap of `pings < 100000`.
  Liveness is judged by `PingLoopLiveNow`.
* **Others:** `ApplyPersistLoop` (`kPersistLoopCap = 60`, `:22734`), `ApplyUntapLoop` (closed form),
  and `RunDrawPingStack`, which stops on the opponent's death or on decking.
* No code references CR 104.4b or CR 732.

### 1.5 Activated abilities, choices, costs, auras, tricks

* **Permanent abilities.**
  * `PermAbilityMode` (`Permanent.h:157`; new modes are APPENDED LAST, because plan signatures and
    ActKey slots hold the numeric values).
  * `PermAbilityTaps` (`Permanent.h:385`) is an exclusion list: any mode not listed taps.
  * The shared resolver is `ApplyPermAbility` (`SpellEffects.h:37549`).
  * Summoning sickness is enforced by `CanTap` (`Permanent.h:923`) and `CanTapNow`
    (`SpellEffects.h:8318`). Enumeration skips a {T} mode when the source is tapped or
    `!CanTapNow` (`TurnSolver.cpp:48098`).
* **Activation windows.**
  * Activations run in a **trailing pass after the plan's casts** (`TurnSolver.cpp:48120-48130`):
    `exec_trailing_activations` on the executor side (`AIEngine.cpp:5971`), `ApplyPermAbility` in
    the rollout (`TurnSolver.cpp:76553`).
  * Instant-speed abilities are "collapsed to the main phase" (`TurnSolver.cpp:47153`). There is no
    general window for responding to our own spell.
  * A **{0} repeatable ability fires at most once per action**: `SpendRepeatActivations`
    `if (per <= 0) break;` (`SpellEffects.h:38891`).
* **Cast riders.** Spellskite's redirect is the model for "an instant-speed thing done in response
  to our own cast". `Action::trick_redirect` (`TurnSolver.h:764`) is a searched cast VARIANT. It
  rides `CastRiders` (`TurnSolver.h:1159`) to the rollout and `StackEntry::trick_redirect`
  (`GameState.h:223`) to the executor, which also adds its extra pip (`AIEngine.cpp:10453`,
  `10806`).
* **"As enters, choose".**
  * `chosen_color` is set in `FireOwnEtbTriggersBody` (`SpellEffects.h:13115-13170`; provider
    `EtbChosenColor`, or the searched `etb_kx` pin for Utopia Sprawl).
  * There is **no "choose a player"** choice.
* **Scry.**
  * The single choke point is `ScryTop` (`SpellEffects.h:49253`) → `ChooseTopDisposition`.
  * Scry is searched only around land drops (`Plan::scry_choice` via `ScriptedTopChoice`,
    `TurnSolver.cpp:75054`).
  * A spell's `cast_scry` is honoured only in `ResolveDirectDamage` (`EffectHandler.cpp:990`) and
    `ResolveDrawSpell` (`:1110`, rollout `TurnSolver.cpp:73196`), and is not searched.
* **Cost reduction.**
  * `EffectiveSpellCostImpl` (`ManaPayment.cpp:4604`) is one function for enumeration and payment,
    evaluated on the live board at cast time.
  * `affinity_for_subtype` (`:4667`) counts OUR permanents of a subtype only.
  * The scalar mana prune bails on affinity (`ManaPruneBound`, `TurnSolver.cpp:55092-55100`).
  * The reducer walks are gated on `deck_has_cost_reducer` (`ManaPayment.cpp:4880`).
  * No "each creature on the battlefield" reducer exists.
* **Auras.**
  * `is_aura`; the host rides `Action::enchant_target` (`TurnSolver.h:637`).
  * The host is SEARCHED: one `CastFromHand` per host from `LegalEnchantTargets`
    (`TurnSolver.cpp:42197-42283`; `SpellEffects.h:6129`).
  * The creature arm of `LegalEnchantTargets` offers only OUR creatures (`SpellEffects.h:6151-6160`).
  * Resolution: `ResolveEnchantTarget` (`SpellEffects.h:6202`).
  * `SweepOrphanedAuras` (`:29255`) implements CR 704.5m. An Aura whose host died stays on the
    battlefield as an inert orphan until the next checkpoint (a disclosed approximation, `:29250`).
* **Tricks.**
  * `solo_target_trick` (`CardDatabase.h:3607`): one cast variant per own creature, the target in
    `enchant_target` (`TurnSolver.cpp:42805`).
  * Resolution: `ResolveSoloTargetTrick` (`SpellEffects.h:30713`) → `ApplyTrickPayload` (`:30571`).
  * There is no indestructible or hexproof payload yet.
* **Grants.**
  * Lords grant keywords through `subtypes_affected` (`CardDatabase.h:108`).
  * There is no "Slivers have [triggered ability]" grant. The closest is Leeching Sliver's counted
    `attack_trigger_life_loss` (`SpellEffects.h:8496-8526`).
  * `CardHasSubtype` (`:10583`) works on tokens.
* **Life.**
  * `GainLife` (`SpellEffects.h:9220`) checks the GLOBAL `PlayersCantGainLifeNow` (`:2165`); there
    is no per-player lock.
  * `OpponentGainsLife` (`:2175`).
* **Mind Stone.** Present (cards.json ~6616, `mana_rock`, `sacrifice_draw_cost {1}`). No change.

### 1.6 Search hosting

* **Plans and actions.**
  * `Action` and `Plan` live in `TurnSolver.h:78` / `:1362`.
  * Per-cast axes ride Action fields: `chosen_x`, `enchant_target`, `trick_redirect`.
  * Mid-resolution choices ride plan pin vectors (`keranos_targets` `:1772`, `incinerator_targets`
    `:1782`).
  * Each new plan field must be folded into `BpCandFingerprint`, `IsApplyEmptyPlan`,
    `PlanIsAxisVariantExceptOrder`, `SamePlan`, `PlanDomCensus`, `ApproxPlanBytes`,
    `SolveWithLookahead`, `ContPins` / `ContPinsOf`, and the two plan-signature builders that
    `test/check_plan_sig_lockstep.py` keeps in step.
* **Greedy permit.** `GreedySite {HorizonLeaf, D0Runner}` (`TurnSolver.h:2279`, `static_assert`
  `:2294`), checked by `greedywindow::Require` (`TurnSolver.cpp:58486`).
* **Horizon.**
  * Below the horizon: `SolveWithLookahead` → greedy `SolveUncachedBody` (`TurnSolver.cpp:59962`).
  * Its lethal sum (`:61627-61660`) includes the provider's `ExtraLethalDamage`
    (`DecisionProvider.h:1436`; generic `return 0`, `DecisionProviders.cpp:5953`).
  * The rollout is `SimulateToEndImpl` (`TurnSolver.cpp:105048`).
* **Providers.**
  * Hierarchy: `DecisionProvider` → `GenericProvider` → `DeckProvider` (requires `Certificate()`)
    → deck classes.
  * Routing: `DetectDecisionProvider` (`DecisionProviders.cpp:28091`; the return chain is around
    28960-29030).
  * The certificate hook is `ProvenWinlessThisTurn` (`DecisionProvider.h:1543`; only Gods
    implements it).
* **Second main.** `GoldFishRunner::DeckUsesSecondMain` (`GoldFishRunner.cpp:57`) and
  `SolveSecondMainInSearch` (`TurnSolver.cpp:8087`).
* **Breakpoints and ordering.**
  * No breakpoint class fires on "the board changed / creatures died".
  * Plan actions are enumerated against the board the plan started from.
  * `Plan::searched_order` (`TurnSolver.h:1456-1465`) already interleaves enablers with a
    destroy-all (the Reverent Silence precedent).

---

## 2. The unified damage-event pipeline

### 2.1 Where it lives and how it is gated

* **New header:** `src/core/DamageReflection.h` (namespace `dmgrx`), included from `SpellEffects.h`
  late enough to see `LethalToughness`, `OnCreatureDies`, `SweepOrphanedAuras` and `GainLife`.
  Forward-declare the few it needs, as `DamageEvents.h` already does.
* **New deck trait:** `GameState::reflect_armed`, stamped in `StampDeckTraits` next to
  `dmg_events_armed`. It is true iff the mainboard or sideboard carries any param introduced here:
  * `reflect_dealt_damage` (≠ None)
  * `grants_reflect_to_subtype`
  * `aura_host_damage_back`
  * `redirect_shield_cost`
  * `redirect_source_pull_cost`
  * `self_ping_damage`
* **Unarmed decks keep the legacy bodies byte-for-byte.** Every routed site tests the bool first.
  Pyroclasm (Giants) and Rolling Earthquake / Pyrohemia (Prevent Damage) are therefore untouched.
  Minotaur arms (it runs Boros Reckoner) but never damages a creature, so no new code runs in its
  games (verify `play-changed=0`, §10).

### 2.2 The event model

```
struct DmgAssign { SourceRef src; Recipient to; int amount; bool combat; };
//   SourceRef:  card + m_number + controller + is_creature, captured by VALUE (LKI) at event time
//   Recipient:  {Player, idx} | {Permanent, m_number}
DealDamageBatch(GameState&, std::vector<DmgAssign> batch)   // ONE simultaneous damage event
```

One call is one **damage event** (CR 120.3 and the rulings above). Every damage source this deck
has goes through it:

| Source | Batch |
|---|---|
| Blasphemous Act / Chain Reaction | one batch: every creature on both sides |
| Doll {T} | one assign: Doll → Doll, 1 |
| each reflection trigger resolution | one assign: reflector (LKI) → its target, N |
| each Guilty Conscience resolution | one assign: GC (LKI) → host, N |
| combat damage to the opponent (armed decks) | the step's assigns, so a GC host's trigger sees it |

The steps inside `DealDamageBatch`, in order:

1. **Replacement (redirection) — CR 614.9, order chosen by us (CR 616.1).** The redirection shields
   (§2.3) are applied to the batch until none applies. A shield is consumed only when it moves
   damage. Redirected damage keeps its **source** and its **combat** flag (the en-Kor ruling). A
   point redirected onto a creature that carries its own shields can be redirected again by a
   *different* shield (each replacement applies once per event, CR 614.5). Termination: each shield
   is consumed at most once.
2. **Aggregate per recipient.** Sum every assignment to the same recipient in this batch. The
   Act's 13 to the Doll plus 13 redirected from a Nomads becomes ONE "Doll is dealt 26" (rulings:
   "triggers once ... that much damage").
3. **Snapshot the trigger table (LKI).** Before anything changes, record:
   * every reflection instance on every damaged creature (its own `reflect_dealt_damage` plus one
     granted instance per Spiteful Sliver we control **at this moment**);
   * every Guilty Conscience attached to a creature that is a SOURCE in this batch, with the total
     damage that creature dealt.

   This is the `tamanoa_lki` idea from `PerformDamageEachCreatureAndPlayer`, generalised.
4. **Apply.**
   * Creatures: `damage += n` (prevention never applies; nothing in this pool prevents).
   * Players: route through one helper, `ApplyDamageToPlayer`. It calls `dmgev::DealDamageEvent`
     when `dmg_events_armed` is set, so Purity and Tamanoa stay right in a deck that has them.
     Otherwise it does `life -= n`, sets `opponent_lost_life_this_turn`, and calls
     `RecordNoncombatDamageToPlayer` for noncombat damage.
   * Nemesis's rider: when a reflection instance with `reflect_player_cant_gain_life` deals damage
     to a player, set that player's new bit `Player::cant_gain_life_rest_of_game`. `GainLife` and
     `OpponentGainsLife` must honour the bit.
5. **SBA — one shared pass, `dmgrx::SbaPass`, used by every pipeline step in BOTH worlds:**
   * the `CounterPingSbaDead` predicate;
   * the card goes to the **controller's** graveyard (matching `GameEngine.cpp:1710`);
   * Equipment detaches;
   * Aura links are cleared and **`SweepOrphanedAuras` runs inline**, so a Guilty Conscience whose
     host died is in the graveyard before any trigger resolves;
   * `OnCreatureDies` (+ `FireOppCreatureDies`);
   * then `dmgev::SbaCheckpoint` (life).

   The executor's own `CheckStateBasedActions` later finds nothing to do, so the two worlds agree.
6. **Game over?** If the opponent is dead, or we are, stop here: no trigger is put on the stack
   (the Doll ruling). The won-lock makes any later damage to us a no-op.
7. **Push the snapshot's triggers** onto an explicit LIFO (`std::vector<PendingTrig>`; the
   `RunDrawPingStack` shape, not recursion), in canonical order (§4.7).
8. **Resolve the stack.** For each popped trigger:
   1. Choose its target lazily (§4.1). Damage to "that creature" whose creature is gone does nothing.
   2. Build the one-assign batch and call `DealDamageBatch` again, which pushes any new triggers on
      top: depth-first, as the real stack would.
   3. Check for game over after each resolution.

### 2.3 Shields (en-Kor) as state

Add a small per-turn list:

```
GameState::redirect_shields: { int on; int to; int8 kind; }
// on = the en-Kor's m_number
// to = the target creature's m_number
// kind 0 = "next 1 damage"
// kind 1 = "next time source S would deal damage to creature T, dealt to this instead"
//          (this kind also carries S and T)
```

* **Lifetime.** Cleared at cleanup in BOTH worlds (the three `damage = 0` reset sites).
* **Byte identity.** The list is empty for every other deck, so it costs nothing there. It is folded
  into the sim key and into dominance **only when non-empty**.
* **Rules.**
  * A 1-point shield whose target is no longer on the battlefield or no longer ours redirects
    nothing; the damage stays. Rules question Q-R1 (§11) asks whether the shield is spent anyway.
    Default here: it is NOT consumed. This is conservative: it can only keep more damage on the
    en-Kor.
  * The source-pull shield applies to ALL the damage source S would deal to T in that event, once.

### 2.4 LKI and the auras

* **A reflector that died in the same event still triggers.** The `PendingTrig` holds its card,
  controller and kind by value. "It deals that much damage" uses that snapshot as the source, so it
  is a creature source for Tamanoa/Purity purposes.
* **Guilty Conscience whose host dies.** The host's reflection trigger then resolves with GC already
  in the graveyard (step 5). GC was not on the battlefield when the LKI host dealt that damage, so
  it does **not** trigger again. A GC trigger that was already pending when its host died still
  resolves, but "that creature" is gone, so it deals nothing.
* **Spiteful Sliver multiplicity is read at step 3**, so a Spiteful Sliver killed by the same
  Blasphemous Act still contributes its granted instance to every Sliver's trigger set for that
  event. The granted ability existed when the event happened.

### 2.5 Who calls it

| Site | Executor | Rollout | Change |
|---|---|---|---|
| Act / Chain / Pyroclasm-shaped sweeps | `EffectHandler.cpp:756` | `TurnSolver.cpp:73677` | inside `PerformDamageAllCreatures`: `if (state.reflect_armed) return dmgrx::Sweep(...)` — both worlds already share the helper |
| Doll {T} | `ApplyPermAbility` (trailing pass, `AIEngine.cpp:7615`) | `ApplyPermAbility` (`TurnSolver.cpp:76553`) | new mode `SelfPing` in the shared resolver |
| Combat damage → GC host | `ResolveCombatDamage` (`Combat.cpp:359`, `:582`) | same (shared) | armed only: collect the step's face assigns, then `dmgrx::AfterCombatDamage` queues GC triggers for damaging hosts |
| Rolling Earthquake in an armed deck | `PerformDamageEachCreatureAndPlayer` | same | creature pass via the pipeline (armed only) |

Every entry point is a shared helper, so lockstep is by construction. The verification is a
`MTG_RFX_VERIFY=1` diagnostic in the `MTG_DMG_EVENT_VERIFY` style: assert that the pending stack and
the shield-consumption bookkeeping are empty at every decision boundary.

---

## 3. Termination and loops

**The loops that exist in this pool:**

* **Doll(opponent) + GC.** Doll is dealt N → it deals N to the opponent → GC deals N to the Doll →
  and so on. The Doll {T} ping enters the same loop: Doll deals 1 to itself, which raises both its
  reflection trigger and GC's trigger.
* **Indestructible (Loran's) Reckoner or Nemesis + GC, aimed at the face.** Same shape, with a
  target chosen each time.
* **Indestructible Reckoner aimed at ITSELF, without GC.** A loop with zero progress. It is optional:
  every iteration re-chooses the target.
* **Doll(us) + GC.** Progress against OUR life; we lose. This is legal and only reachable by a human
  choosing themselves.

None of these is a CR 104.4b draw:

* Every progressing loop removes ≥ 1 life per cycle from a finite total and ends at the SBA.
* The zero-progress loop contains a choice in every cycle (the target), so it is not a loop of
  mandatory actions.

**Design.** Iterate for real; no closed form.

* Each cycle costs two `DealDamageBatch` calls on a one-creature batch.
* The opponent's life is at most ~30 in the goldfish (20, or 30 in Two-Headed Giant (2HG);
  `GameSetup.h:66`; the opponent never gains life).
* So a loop ends within `2 × life / N` resolutions: ≤ 60 for the 1-damage ping loop, fewer for
  larger N.

A closed form (`life -= N × ceil(life / N)`) would equal the iteration's final state exactly. It is
not worth a second code path that must stay in lockstep unless a profile shows the loop is hot.

**Safety cap.** `kRfxResolutionCap = 1 << 16` trigger resolutions per top-level `DealDamageBatch`,
**plus a no-progress rule**:

* if 64 consecutive resolutions change no player's life and kill no creature, stop resolving and
  log `rfx-noprogress`;
* in the autonomous engine this is unreachable, because the candidate folds (§4.1) never offer a
  non-amplifying own target;
* in human play it is the "player chooses to stop" (the next prompt would force a face target).

**Proof the cap never changes a reachable win.** A loop that can kill the opponent removes ≥ 1 life
per ≤ 2 resolutions. Killing from life L ≤ 2^14 needs ≤ 2^15 < cap resolutions. The no-progress rule
fires only when no life changed for 64 resolutions, and a killing loop changes life every ≤ 2. So
the cap can only cut a sequence that could never kill anyone.

**2HG.**
* Team life is 30 in one pool (`players[1]`, `OpponentHeads() == 2`).
* "Choose a player" and "target opponent" pick a head, and both heads share the pool, so a single
  target is ×1 (the `etb_damage_any` convention at `SpellEffects.h:13861`).
* Nothing in this pool says "each opponent", so nothing doubles.
* The bound above holds at L = 30.

**When the loop cannot kill.** That is only the zero-progress self-target cycle (handled above) and
"chosen player = us" (we die; correct). A GC host that dies to its own GC damage (a non-indestructible
Reckoner or Nemesis) gives a finite chain: 2N total for the first hit, as the rulings imply. The
pipeline handles it with no special case.

---

## 4. The search decisions, how each is searched, and the sound folds

The house rule applies to all of these:
* The ENGINE enumerates every legal option and applies only SOUND folds.
* The DECK PROVIDER may restrict or order.
* Human play sees every option (`MTG_UNPRUNE`).

**Definitions.**
* **mult(T)** — the amplification of a target T for an amount N:
  * the opponent's face: 1;
  * an own creature T: N reaches the opponent `instances(T)` times, where `instances` = T's own
    reflection plus one per Spiteful Sliver if T is a Sliver;
  * ∞ if T carries a GC and survives the damage, or is the Doll(opponent) with a GC — a live loop
    (§3).
* **survival cost(T)** — whether N (with T's marked damage) kills T. Indestructible: never.

### 4.1 Reflection-trigger targets (Reckoner: any; Nemesis: any other; Sliver: player; Maniac: opponent; Doll: fixed)

**Option set at resolution:**
* the opponent's face (one pool, also in 2HG);
* our face;
* every creature on the battlefield on both sides (Reckoner: including itself; Nemesis: excluding
  itself);
* planeswalkers (none exist).

The Maniac and the Doll have no choice.

**The lazy-target equivalence (why choosing at resolution is exact).** Rules-wise the targets are
chosen when the triggers go on the stack, all before any of them resolves. Choosing lazily, as each
resolves, is equivalent here because three things hold during a cascade:

* (i) no permanent enters the battlefield, so the legal set only shrinks;
* (ii) everything is deterministic and fully known to us;
* (iii) a trigger whose target died before it resolved does nothing, which "aim at the opponent's
  face" weakly dominates.

So every lazy sequence is realisable by some up-front assignment plus order, and every up-front
assignment is matched or beaten by a lazy one. This is also how Keranos already works
(`scripted_boundary_keranos`, "resolution-time damage target"). It removes the ordering decision
too (§4.7).

**SOUND folds (proofs).** These can live in the deck provider; they are equally valid as engine
folds behind the stated predicate.

* **F1. Our own face is dominated by the opponent's face** while no permanent rewards damage to us:
  no `prevent_noncombat_to_self_gain`, and no `noncreature_damage_lifegain` that this source would
  trigger. Proof: our life only matters through dying, and nothing in the game reads it downward
  otherwise. Face damage is monotone good in the goldfish.
* **F2. An own creature T with mult(T) = 1 that survives N is identical to the face, except that T
  carries marked damage. An own creature with mult(T) = 1 that dies to N is dominated by the face.**
  Either way the face weakly dominates. Proof: the same N reaches the opponent once in both lines,
  and the face line keeps T undamaged.

  Corollary: **the Doll without GC ≡ the face** (identity fold: it is indestructible and its recipient
  is fixed).
* **F3. Reckoner targeting itself.** It falls under F2: mult = 1, a zero-progress re-trigger. It is
  an amplifier only with GC (mult = ∞).
* **Kept:** the face, plus every own creature with mult(T) > 1:
  * a GC host that survives N, or a Doll(opponent) with a GC — a win, so it dominates everything;
  * a Sliver with ≥ 2 Spiteful instances (×k).

  An amplifier that dies to N still delivers its k×N through LKI.

  Among kept options, ∞ dominates all finite ones. Among finite ones, larger k×N to the face now
  versus the survival of the target is a real trade-off and stays searched.

**Not provably dominated → PROVIDER restriction (heuristic, measured in Stage 5e, PROVISIONAL).**
Opponent creatures (Spirits, spawns) as targets. Killing one lowers a later same-turn Chain
Reaction's X and raises a later Blasphemous Act's cost; it never adds damage now. Pruning it is
almost surely right, but "fewer creatures" can, in contrived lines, keep one of our creatures alive.
So it is a narrowing, not a proof.

**Search hosting.** A new plan pin vector `Plan::reflect_targets`, using the Incinerator shape
exactly:
* the resolver reads `ResolveProvider(state).ReflectTargetCandidates(state, trig, legal)`, a new
  hook whose default returns the full legal set;
* it takes `pins[cursor++]` if that pin is legal, else the provider's FRONT option — legal only
  beyond the horizon or where the list is a singleton;
* the probe-and-fan block in `AppendSubdecisionAxes` clones the plan once per candidate at the next
  unpinned ordinal.

Add the field to every plan-fold site listed in §1.6 and to `ContPins`. Pin by stable id (`m_number`;
player = −1 − idx), not by rank: ranks shift as creatures die.

**Expected branching.** After a Blasphemous Act, only indestructible creatures (the Doll, Loran'd
creatures) remain to be targeted. After F1–F3 the list is almost always the singleton {face}, or
{Doll+GC} = win. So the fan multiplies plans by ~1.0.

The probe costs one extra `ApplyPlanDirect` per plan that can raise a trigger, so skip it on a cheap
pre-check: no GC in play, hand or plan, AND ≤ 1 Spiteful Sliver → every candidate list is a
singleton → no probe. The pre-check is a sound shortcut, not a fold.

### 4.2 en-Kor {0} shields: how many, onto what, when

* **Option set.** For each en-Kor E and each damage event about to hit E with d points: an
  allocation of 0..d points (shields are free and unlimited) among the creatures we control. "Any
  number of times prior to damage" (ruling).
* **Timing fold (sound).**
  * Shields last "this turn" and nothing can happen between our sorcery-speed main-phase action and
    our sweeper's resolution.
  * So "activate in response to my own sweeper" ≡ "activate just before casting it".
  * The decision is hosted **as a rider on the sweeper's cast** (the Spellskite `trick_redirect`
    shape), applied at resolution before the damage.
  * Shields against other damage (a reflection trigger aimed at an en-Kor, GC on an en-Kor attacker)
    are dominated by aiming that damage at the redirect target directly (F2), so no other window is
    needed.
  * Leftover shields stay in `redirect_shields` and apply to later damage this turn, as the rules
    say.
* **SOUND fold F4.** Suppose a reflector R in the batch is indestructible, or already takes lethal
  damage from the sweeper itself (doomed). Then "redirect ALL d of E's damage onto the max-mult such
  R" weakly dominates every other allocation for E.

  Proof:
  * each point moved onto R reaches the opponent mult(R) ≥ 1 times, and a point left on E reaches it
    0 times (en-Kors are not reflectors);
  * E is dealt 0, so it survives — at least as well as under any other allocation;
  * R's survival is unchanged (indestructible, or dead anyway);
  * no other state differs.

  Ties between equal-mult R's differ only in which permanent carries marked damage → identity fold.
  Under a Blasphemous Act every non-indestructible reflector is doomed, so F4 nearly always applies:
  **one allocation per en-Kor.**
* **When F4 does not apply** (a Chain Reaction with small X, where a reflector R would survive its own
  X): offer the landmarks:
  * {0};
  * {all → R} for each distinct live reflector class;
  * {just enough to keep E alive → R} (d − (t−1−marked), clamped).

  Partial allocations between two reflectors are dominated by the better of the two in the goldfish:
  the amounts are additive and the targets independent, except where survival thresholds bind, and
  the landmarks above cover those thresholds.
* **The provider may order** the landmarks. **Human play gets the full allocation** through the
  viewer's `divide` decision (§6).
* **Symmetry.** Identical en-Kors (4× Nomads) facing identical d get the same landmark set. Fold the
  permutation (the plan carries a multiset).
* **Expected branching.** ×1 under an Act; ≤ (reflector classes + 2) per distinct en-Kor class under
  a Chain Reaction.

### 4.3 Shaman en-Kor {1}{W}: the source pull

* **Option set.** Activation count m (0..floor(available / 2)), and for each activation a target
  creature T. The source is "a source of your choice".
* **Sound folds.**
  * **Source = our sweeper.** It is the only damage source this deck aims at its own board. Hosting
    the pull as part of the sweeper's rider makes S implicit.
  * **T must not be a reflector.** Moving N from reflector R to Shaman, then (via Shaman's {0}s, F4)
    to the Doll or R', yields mult(dest) × N in place of mult(R) × N. That is never more when R is
    already a max-mult reflector, and costs {1}{W}. Pulling from a doomed reflector is therefore
    dominated.
  * **Kept:** T among our non-reflector creatures (Nomads, another en-Kor) and the opponent's creatures (Spirits, spawns).

    Pulling from a Spirit keeps the Spirit alive (+1 creature for the next Act or Chain Reaction).
    Pulling from our Nomads keeps Nomads alive, but its free shields already do that, so it is
    dominated by pulling from a Spirit when one exists. That is NOT a proof (Nomads' own shields
    could go elsewhere). Provider order: Spirits first.

    Each pull adds d_T to the Shaman's incoming damage, which its {0}s then forward (F4).
* **Hosting.** Part of the same sweeper rider: `RedirectRider{ allocations[], pulls[] }`, with the
  pulls' {1}{W} × m added to the cast's bill, exactly as `AddSpellRedirectPip` adds Spellskite's
  pip.
* **Expected branching.** m ∈ {0..M} (M usually ≤ 2) × target class (2) → ≤ ~5 variants per sweeper.

### 4.4 Guilty Conscience host

* **Option set.** Every creature (both sides).
* **Existing engine limitation.** `LegalEnchantTargets` offers only our creatures. That is a generic
  restriction; for GC it is sound, because an opponent's creature never deals damage in the goldfish
  (it never attacks, blocks or activates), so GC there does nothing. Disclose it.
* **Folds.** The deck provider may restrict to the Doll, the reflectors, and creatures able to attack
  this turn. GC on a non-reflector that cannot attack is inert this turn but not forever, so this is
  a restriction, not a proof.
* **Hosting.** The existing aura host variants.
* **Branching.** ×(own creatures), usually 2–4.

### 4.5 Loran's Escape target and timing

* **Option set.** Every artifact or creature, both sides. That includes Mind Stone (inert apart from
  the scry), the Doll (already indestructible, so ≡ Mind Stone), and Spirits (survive the next Act:
  +1 creature).
* **Hosting.** `solo_target_trick` variants per target. Today the trick target set is own creatures
  only (`trick_own_target_only`); for Loran's add artifacts. The opponent-creature arm is a provider
  question (below).
* **Folds.** Targets with no effect (Mind Stone, the Doll, an already-indestructible creature) are
  mutually identical ("scry 1 only") → keep one representative.
* **Timing.** The sweeper is a sorcery cast by us; nothing happens between "cast Loran's in main" and
  "in response to the sweeper" → same fold as §4.2. Before-vs-after the sweeper is the cast-order
  axis (§4.8).
* **The scry.** See §11 Q-U3: today a spell's scry is a provider pick, not searched.

### 4.6 Stuffy Doll's chosen player, and its {T}

* **Chosen player — sound fold, gated.** The opponent dominates ourselves while no permanent rewards
  damage to us (the same predicate as F1): every point the Doll reflects then reaches the opponent
  instead of us. Implement `Permanent::chosen_player` (int8, −1 = none).
  * Autonomous play takes the opponent whenever the predicate holds.
  * If a self-damage payoff is ever present, offer both as an ETB variant (the `etb_kx` pin shape)
    rather than picking.
  * In 2HG the two heads share the team pool, so "either head" is one option.
  * Human play: the `target` decision with a players-only legal set (§6).
* **{T}: deal 1 to itself.** A new `PermAbilityMode::SelfPing` (append LAST). It taps, so
  `PermAbilityTaps` needs no change; `CanTapNow` enforces summoning sickness. It is a plan action.
* **Sound fold F5: pinging weakly dominates not pinging, when done in the trailing pass of the turn's
  last main phase.**
  * The Doll has power 0, so attacking with it deals no damage and raises no GC trigger.
  * It has no other {T} use.
  * It is indestructible, so the self-damage is harmless.
  * The reflection is 1 to the opponent, and a win if a GC is on it.
  * Doing it after every cast in the phase means a GC cast that phase is already attached.

  So the provider prunes the no-ping twin. Human play still sees both.

### 4.7 Order of simultaneous triggers

* The lazy-target equivalence (§4.1) makes order irrelevant to the outcome under the folds. Kept
  targets are the face, which commutes, or an ∞ amplifier, which wins.
* The pipeline uses one canonical order: battlefield order of the reflectors, then the GC triggers.
  This is NOT surfaced.
* This is the one ordering we deliberately fix. Disclose it in 6a with the proof. Under
  `MTG_UNPRUNE`, where non-face creature targets reappear, order could matter in contrived
  Chain Reaction lines; record that as a PARTIAL.

### 4.8 Sweeper timing (Blasphemous Act / Chain Reaction)

* **Cast order within the main** — which creatures, Loran's and GC are cast before the sweeper, and
  which after. Creatures cast before reflect and cheapen the Act; creatures cast after survive.
  * Neither dominates, so the order is searched: `Plan::searched_order`, the destroy-all interleave
    precedent.
  * Any narrowing is a CAST-ORDER rule, which is **USER-OWNED**: author it and present it, never
    adopt it.
* **Main 1 vs main 2** — see §7.
* **Chain Reaction's X** is not chosen; it is read at resolution.
* **Act's cost** is read at cast time from the live board, through `EffectiveSpellCostImpl`.

### 4.9 Boros Reckoner's {R/W}: first strike

* Not offered. See the deferral in §9: it is provably outcome-inert against a non-blocking opponent.

**Branching summary for an Act turn with 4 own creatures:**
* plan variants ≈ (cast-order variants) × (rider variants ≈ 1–5) × (reflect fan ≈ 1);
* GC host and Loran's target are ×(own creatures) only when those cards are in the plan.

---

## 5. Rollout policy (beyond the horizon) and lethal visibility

**The permitted greedy** (`GreedySite::HorizonLeaf` / `D0Runner` only):
* reflection targets: the provider's FRONT candidate (∞ amplifier > face);
* rider: the F4 allocation, else "all → max-mult live reflector", and pulls = 0;
* GC host: the Doll if present, else the reflector with the most damage expected;
* Loran's: the highest-mult non-indestructible reflector;
* Doll ping: always (F5).

Each pick is a provider ORDER, used only where the permit allows it.

**Lethal projection — `StuffyDollProvider::ExtraLethalDamage(state, casting)`, an optimistic bound
checked by execution:**

```
proj = 0
if a Doll(opp) [on board, or in `casting`] and a GC [on it, or GC in `casting`] and some damage
   reaches the Doll this turn (untapped non-sick Doll -> ping; or a sweeper in `casting`; or an
   en-Kor + sweeper)                                                          -> return kHuge (win)
if a sweeper S is castable [in `casting`]:
    d = 13 (Act) or creatures-at-resolution (Chain: board + casting + Spirits)
    for each own reflector R (board + casting):  proj += d * instances(R)      // Sliver k counts
    for each en-Kor E: proj += d                    // F4: E's d forwarded to a reflector
    for each affordable Shaman pull: proj += d
+ Doll ping: +1 per untapped, non-sick Doll(opp)
```

* Wire it into the greedy `SolveUncachedBody` lethal sum (`TurnSolver.cpp:61627-61660`), which is
  where the horizon picker otherwise cannot see that an Act wins: the Act deals 0 to the face
  directly.
* `HasExtraLethalModel() → true`.
* **Inside the window no projection is needed.** `ApplyPlanDirect` resolves the real pipeline and
  `OpponentHasLost` sees the kill.
* The `ComboOffPossible` / `plausible` gate (`TurnSolver.cpp:124790-124830`) applies only to the
  mechanical combo-off route, which this deck does not use.

---

## 6. Viewer (2c-ter)

| Choice | Bucket | Mechanism |
|---|---|---|
| Sweeper / creature / GC / Loran's cast order, main 1 vs 2 | A | `main_phase` plans |
| GC host | A | `main_phase` aura host variants (`enchant_target`) |
| Loran's Escape target | A | `main_phase` solo-trick variants. Add artifact targets to the legal set for this card. |
| Loran's scry 1 | A | `scry` (`g_play_top_chooser`) — wire `cast_scry` into `ResolveSoloTargetTrick` (shared) |
| Reflection target (Reckoner any; Nemesis any other; Sliver player) | A | `target` via `g_play_target_chooser` with `ChosenTarget{kind 0 player / 1 permanent}`, called at the lazy resolution point — the Keranos call site shape (`SpellEffects.h:30077`). Maniac and Doll: forced, not prompted. |
| Doll's chosen player | A | `target` with a players-only legal set (the Heroes' Reunion / Rest for the Weary shape, `WriteTargetDecisionJson` `mills_target_player` / lifegain branch, `main.cpp:4229`) at the ETB choice in `FireOwnEtbTriggersBody` |
| Doll {T} ping | A | `main_phase` plan action. Needs a **CheckLine verb** (`selfping=<source>`; DECISIONS.md "give a kind its own verb", ~line 352) because two Dolls may be on board. |
| en-Kor shield allocation | A (type), new call site | `divide` (`WriteDivideDecisionJson`, `main.cpp:4196`): "redirect up to d of the sweeper's damage from <en-Kor> among your creatures; the remainder stays". It is called from the rider resolution when the human is playing. |
| Shaman {1}{W} pull count | A | `main_phase` sweeper cast variants (m) |
| Shaman pull target creature | A | `target` via `g_play_loyalty_chooser` (`LoyaltyTargetChooser`, board pick), at rider resolution |
| Trigger order | — | not surfaced (§4.7); disclose |

* **No bucket-B type is needed.** Two existing types (`divide`, `target`) get new call sites; each is
  wired at the SHARED resolution site (`dmgrx::` in `SpellEffects.h`), never `AIEngine`'s autonomous
  path.
* Add rows to `tools/play/DECISIONS.md` and the param → type mapping to
  `scripts/audit_viewer_decisions.py`'s manifest.
* For Stage 5h, force each type in a scenario.

---

## 7. Second main (2c-bis)

**Yes, the post-combat main is a real decision for this deck.**

* Attacking first and then sweeping in main 2 gets the attackers' combat damage AND the sweeper's
  reflection. Attackers are still on the battlefield, merely tapped, and are dealt 13 like anything
  else.
* A sweeper in main 1 kills them before combat.
* GC on an attacker resolves its loop in combat, before main 2.

Within one turn, in the goldfish, "sweep in main 2" weakly dominates "sweep in main 1" *for the
sweeper itself*. Nothing in this pool needs the board cleared before combat, and no new information
arrives in combat. But creature casts, Loran's and GC interact with that order, so the honest
statement is "the split is searched".

**Wiring:**
* `GoldFishRunner::DeckUsesSecondMain` detects a new param pair: a symmetric sweeper (`damage_all_creatures`
  or `damage_all_creatures_count_creatures`) together with `reflect_dealt_damage` in the same deck.
* Gate it behind `MTG_RFX_SECOND_MAIN`.
* **Main-split is USER-OWNED.** Recommend ON, show the measurement, and let the user decide (§11 Q-U4).

---

## 8. Provider (Stage 4a)

**`StuffyDollProvider : DeckProvider`.**
* Derive from `DeckProvider`, not from any thematic sibling.
* The deck most likely routes to Generic today: none of its params trips an existing signature.
  `slivers` keys on `grants_replicate_to_subtypes || affinity_for_subtype`; Prevent Damage keys on
  lifegain params. Confirm with `scripts/provider_audit.py`.

**Signature.** OR of gated params from several different cards, routed ABOVE the existing chain
(the Dragons block shape):

```
p.self_ping_damage > 0                       // Stuffy Doll
|| p.aura_host_damage_back                   // Guilty Conscience
|| p.redirect_shield_cost.has_value()        // Nomads / Shaman en-Kor
|| p.grants_reflect_to_subtype               // Spiteful Sliver
```

* **Not** `reflect_dealt_damage` alone: Boros Reckoner carries it and would pull Minotaur in.
* **Not** Blasphemous Act's reducer: a generic wrath any deck might add.

**Hooks:**
* `ReflectTargetCandidates`: F1–F3 (sound) + Spirit prune (restriction); order ∞ > face.
* `RedirectRiderCandidates`: F4 (sound) + landmark order.
* `ShamanPullCandidates`: order Spirits first.
* GC host and Loran's restrictions (§4.4–4.5).
* The F5 ping-twin prune.
* `ExtraLethalDamage` (§5); `CastOrderRank` (USER-OWNED; present only).

**Certificate — `ProvenWinlessThisTurn` (a proof; one-sided).** Return true iff the UPPER bound on
this turn's damage is below the opponent's life. UB is the sum of:

* combat: the summed power of creatures able to attack, plus any castable haste power (Nemesis 3
  per copy in hand, if affordable);
* sweeper: ∞ if any sweeper is castable this turn AND (a Doll is on board or castable with a GC
  reachable, OR any en-Kor or reflector is reachable). Otherwise, for a castable sweeper:
  `d_max × (own reflectors + castable reflectors) × max(1, Spiteful count + castable Spitefuls)`
  plus `d_max × (en-Kors + castable en-Kors + affordable pulls)`;
* Doll ping: +1 per untapped non-sick Doll, or ∞ if a GC can be attached to it this turn.

Castability uses the total available mana (lands + Mind Stone + float), Act's cost at its maximal
discount (every creature on the battlefield plus every castable creature), and `d_max = 13`. Chain
Reaction's `d_max` is (board creatures + castable creatures + Orchards' Spirits).

The bound is crude, but early turns (T1–T3, opponent at 20) are provably winless under it, which is
where the search spends most of its horizon-edge nodes. Ship `Certificate() = NotAssessed` first,
and add this as a separate measured step: one-sided, byte-identical labels, speed only.

---

## 9. Per-card specification

Shared new params (`CardParams`, `CardDatabase.h`; read in `BuildParamsFromJson`, `CardDatabase.cpp`):

| Param | Type | Meaning |
|---|---|---|
| `reflect_dealt_damage` | enum string: `chosen_player`, `target_opponent`, `any_target`, `any_other_target`, `target_player` | "Whenever this creature is dealt damage, it deals that much damage to ..." |
| `reflect_player_cant_gain_life` | bool | Nemesis's rider |
| `grants_reflect_to_subtype` | enum string (same values) + `subtypes_affected` | Spiteful Sliver's grant (one instance per granting permanent) |
| `etb_choose_player` | bool | "As this creature enters, choose a player" |
| `self_ping_damage` | int | Doll's "{T}: deals N damage to itself" (→ `PermAbilityMode::SelfPing`) |
| `aura_host_damage_back` | bool | GC |
| `redirect_shield_cost` | ManaCost | en-Kor's "{0}: next 1 damage ... to target creature you control" |
| `redirect_source_pull_cost` | ManaCost | Shaman's "{1}{W}: next time a source of your choice ... dealt to this creature instead" |
| `cost_less_per_creature_on_battlefield` | bool | Act |
| `damage_all_creatures_count_creatures` | bool | Chain Reaction |
| `grants_temp_indestructible`, `grants_temp_hexproof` | bool | Loran's trick payload (hexproof: recorded, inert) |

| Card | Tier | cards.json shape | C++ changes | Clauses → model | Deferral (PROVISIONAL) |
|---|---|---|---|---|---|
| **Stuffy Doll** | 3 | `custom`; types Artifact, Creature; subtypes Construct; 0/1; keywords `["Indestructible"]`; `mana_cost "{5}"`; params `etb_choose_player: true`, `reflect_dealt_damage: "chosen_player"`, `self_ping_damage: 1` | `Permanent::chosen_player`; ETB choice in `FireOwnEtbTriggersBody`; `PermAbilityMode::SelfPing` + ModeSpec row in `CollectActions` + `ApplyPermAbility` case; CheckLine verb | indestructible: keyword. Choose a player: §4.6 fold. Reflection: pipeline, recipient = `chosen_player`. {T}: SelfPing → `DealDamageBatch` | none |
| **Nomads en-Kor** | 3 | `vanilla_creature`; Kor Nomad Soldier; 1/1; `"{W}"`; `redirect_shield_cost: "{0}"` | `GameState::redirect_shields`; replacement step; sweeper rider (`Action`/`CastRiders`/`StackEntry`); `divide` call site | {0} shield → rider allocation (§4.2), state shield, CR 614.9 replacement; "this turn" → cleared at cleanup; "target creature you control" → own creatures at activation, re-checked at application | PARTIAL: shields are activated only as a rider on our own sweepers. Other windows are dominated (F2). Disclose. |
| **Shaman en-Kor** | 3 | as Nomads; Kor Cleric Shaman; 1/2; `"{1}{W}"`; `redirect_shield_cost "{0}"`, `redirect_source_pull_cost "{1}{W}"` | pull shield kind 1; rider pulls + their bill | {0}: as Nomads. {1}{W}: rider pull, source = the sweeper (§4.3) | PARTIAL: the source is always our sweeper (the only source aimed at our board). Disclose. |
| **Mogg Maniac** | 2 | `vanilla_creature`; Goblin; 1/1; `"{1}{R}"`; `reflect_dealt_damage: "target_opponent"` | none beyond the pipeline | "target opponent or planeswalker": the single opponent pool; no planeswalkers exist | none |
| **Boros Reckoner** | 2 | keep entry; `mana_cost "{R/W}{R/W}{R/W}"` (verbatim); add `reflect_dealt_damage: "any_target"`; rewrite the bracket note (drop "inert") | none beyond the pipeline | reflection: pipeline + §4.1 | **{R/W}: first strike — PROVISIONAL deferral, provably outcome-inert.** First strike only changes which combat-damage step its damage lands in; with no blockers the amount to the player is identical, and any trigger it raises resolves inside the same combat before main 2, so the win TURN cannot change. Not offered (it costs mana for nothing). Needs USER sign-off. |
| **Screaming Nemesis** | 2 | `vanilla_creature`; Spirit; 3/3; `"{2}{R}"`; keywords `["Haste"]`; `reflect_dealt_damage: "any_other_target"`, `reflect_player_cant_gain_life: true` | `Player::cant_gain_life_rest_of_game` + checks in `GainLife` / `OpponentGainsLife` | haste: keyword. Reflection: pipeline. Can't gain life: per-player bit, set when its reflection damages a player | none. The bit is implemented, not deferred. It is goldfish-inert (the opponent never gains life, and F1 never aims at us), but cheap. |
| **Spiteful Sliver** | 2–3 | `vanilla_creature`; Sliver; 2/2; `"{2}{R}"`; `grants_reflect_to_subtype: "target_player"`, `subtypes_affected: ["Sliver"]` | the pipeline counts granted instances at the snapshot | "Sliver creatures you control have ...": own Slivers, itself included, one instance per Spiteful Sliver. "target player or planeswalker": the §4.1 axis (F1 leaves the opponent) | none |
| **Blasphemous Act** | 2 | `custom` sorcery; `"{8}{R}"`; `damage_all_creatures: 13`, `cost_less_per_creature_on_battlefield: true` | reducer in `EffectiveSpellCostImpl` (count `IsCreature() \|\| is_animated` on both sides; `reduce_generic`, so it never goes below {R}); `ManaPruneBound` bail + the `:55309/:55382` sites; `deck_has_cost_reducer` stamp; sweep via the pipeline | cost: locked at cast (`EffectiveSpellCost` runs before payment, so payment-time Orchard taps or sacrificed creatures don't count — correct per the ruling); 13 to each creature: pipeline | none |
| **Chain Reaction** | 2 | `custom` sorcery; `"{2}{R}{R}"`; `damage_all_creatures_count_creatures: true` | dispatch in both sweeper sites: X = creature count at resolution | X at resolution (ruling) | none |
| **Guilty Conscience** | 3 | `custom`; Enchantment — Aura; `"{W}"`; `is_aura: true`, `aura_host_damage_back: true` | snapshot step 3 (source = a host); combat hook (armed); loop guard | enchant creature: aura host variants. "Whenever enchanted creature deals damage": every pipeline event where the host is a source, plus combat damage. "this Aura deals that much damage to that creature": GC (noncreature source, LKI) → host | PARTIAL: host offered among our creatures only (existing `LegalEnchantTargets`); provably inert on an opponent creature. Disclose. |
| **Loran's Escape** | 2 | `custom` instant; `"{W}"`; `solo_target_trick: true`, `grants_temp_indestructible: true`, `grants_temp_hexproof: true`, `cast_scry: 1`; legal targets artifact or creature | payload in `ApplyTrickPayload`; artifact arm in the trick target set; `cast_scry` in `ResolveSoloTargetTrick` (shared, both worlds) | indestructible: `temp_indestructible`. Hexproof: recorded, inert (the opponent never targets; our own triggers may still target it — hexproof only stops opponents). Scry 1: `ScryTop` | PARTIAL: hexproof inert, disclosed. Opponent creatures as targets: a provider restriction to decide (§4.5). |
| **Forbidden Orchard** | — | unchanged for now | none in the first pass (§12) | see §12 | USER question Q-U2 |
| Mind Stone | — | unchanged | — | — | — |

**`mana_cost` strings above are verbatim from Scryfall.** Stage 2d-bis's cost audit must pass.

---

## 10. Implementation plan

Each step:
* builds and passes smoke on its own;
* is sized for one implementer well under 250K context;
* ends with `./build.sh`, the unit and scenario tests named, `test/scenarios.sh`, and smoke;
* is reviewed against the rules skill before commit (2d).

**Step 1 — Pipeline core, no cards.**
* Work:
  * `src/core/DamageReflection.h`: `DealDamageBatch` with an empty replacement step, aggregation,
    snapshot (no trigger kinds yet), `ApplyDamageToPlayer`, `SbaPass`, the LIFO, the cap and the
    no-progress rule.
  * `GameState::reflect_armed` + stamping.
  * `PerformDamageAllCreatures` routes to `dmgrx::Sweep` when armed.
  * `MTG_RFX_VERIFY`.
* Tests: `test/unit/test_rfx_pipeline.cpp`, against a synthetic armed GameState:
  * simultaneous kill;
  * indestructible survives;
  * controller's graveyard;
  * an Aura goes to the graveyard in the same pass;
  * Equipment detaches;
  * `OnCreatureDies` fires once per death.
* Risk: none (unarmed). Smoke `play-changed=0` expected for every deck.

**Step 2 — Reflectors and sweepers with the singleton-fold target.**
* Work:
  * params `reflect_dealt_damage`, `reflect_player_cant_gain_life`, the cost reducer,
    `damage_all_creatures_count_creatures`;
  * trigger kinds in the snapshot;
  * the `ReflectTargetCandidates` hook: generic default = full legal set; for now the resolver
    takes the FRONT only when the list is a singleton, and otherwise ABORTS in-window (a tripwire,
    so no greedy can slip in before step 3);
  * `Player::cant_gain_life_rest_of_game`;
  * cards.json: Mogg Maniac, Boros Reckoner (update), Screaming Nemesis, Blasphemous Act, Chain
    Reaction.
* Tests: `--scenario` json files:
  * `stuffy_act_two_reflectors_26.json` (Maniac + Reckoner + 4 Spirits = 6 creatures → Act
    costs {2}{R}; 13 + 13 = 26 ≥ 20 → dead);
  * `stuffy_chain_x_counts_spirits.json`;
  * a unit test for Act's cost floor at {R} and the cost lock (an Orchard Spirit created during
    payment does not discount);
  * a unit test for LKI (a reflector killed by the same Act still deals 13).
* Risk:
  * Minotaur (Boros Reckoner) arms but has no creature damage → GT byte-identical expected; verify
    smoke and regression Minotaur rows unchanged. Also check that no `EvalCard` / `card_scores`
    path reads the new param.
  * `EffectiveSpellCostImpl` touches every deck → the new branch is gated on the param, and
    `deck_has_cost_reducer` stays false elsewhere.

**Step 3 — Searched reflection-target axis.**
* Work:
  * `Plan::reflect_targets` (stable ids), the pin cursor/probe scope, the probe-and-fan in
    `AppendSubdecisionAxes` with the singleton pre-check;
  * every plan-fold site (§1.6) + `ContPins`, and `check_plan_sig_lockstep.py` passes;
  * the viewer `target` call site at resolution;
  * remove the step-2 tripwire.
* Tests:
  * a unit test that a non-singleton list fans and the pins replay identically in the executor (an
    executor/rollout same-outcome check on one seed);
  * a scenario with two Spiteful Slivers + Reckoner where targeting a Sliver beats the face.
* Risk: Keranos and Incinerator share `AppendSubdecisionAxes` → their blocks stay untouched; Gods and
  Burn smoke rows unchanged.

**Step 4 — Stuffy Doll.**
* Work: `etb_choose_player` + `Permanent::chosen_player` (sim key fold only when ≥ 0); SelfPing mode
  (appended last); CheckLine verb; viewer players-only `target`; cards.json.
* Tests:
  * a unit test: summoning-sick Doll offers no ping;
  * a scenario: Doll + Act → 13 to the chosen player;
  * a scenario: Doll ping → 1.
* Risk: `PermAbilityMode` numeric append only.

**Step 5 — Guilty Conscience + loops.**
* Work: `aura_host_damage_back`; snapshot of host-sources; the combat hook at `Combat.cpp:359/582`
  (armed only); cards.json.
* Tests:
  * scenarios: Doll + GC + ping kills from 20, and from 30 in 2HG (team pool);
  * GC on a non-indestructible Reckoner hit by 3 → exactly 2×3 to the face (GC to the graveyard
    before the LKI trigger);
  * GC on an attacking Nemesis → 3 + 3 + 3;
  * a unit test that the no-progress rule fires only on a constructed zero-progress cycle.
* Risk: `ResolveCombatDamage` is shared by every deck → strictly behind `reflect_armed`; smoke
  combat decks unchanged.

**Step 6 — Spiteful Sliver.**
* Work: `grants_reflect_to_subtype` (count instances at the snapshot); cards.json.
* Tests:
  * scenario: 2 Spiteful + Act → 4 × 13;
  * unit: a Spiteful killed by the same Act still grants.
* Risk: the Slivers deck (other Slivers, no Spiteful) is unaffected; the param is new.

**Step 7 — Loran's Escape.**
* Work: trick payload, artifact targets, `cast_scry` in `ResolveSoloTargetTrick` (both worlds);
  cards.json.
* Tests: scenario: Loran's on Reckoner + GC + Act → loop win; unit: the scry fires in both worlds.
* Risk: other `solo_target_trick` cards — the new payload flags default false; `cast_scry` is 0
  elsewhere.

**Step 8 — Nomads en-Kor shields.**
* Work: `redirect_shields` (cleanup reset in the three sites, key fold when non-empty); the
  replacement step; the `RedirectRider` on sweeper casts (`Action`, `CastRiders`, `StackEntry`,
  plan signatures); the F4 fold + landmarks; the viewer `divide` call site; cards.json.
* Tests:
  * scenario: Nomads + Doll + Act → Doll dealt 26, Nomads survives;
  * unit: a shield whose target has died does not redirect;
  * unit: chained shields (E1 → E2 → Doll) consume each shield once.
* Risk: plan signature lockstep (checker).

**Step 9 — Shaman en-Kor pulls.**
* Work: kind-1 shields, rider pulls with their bill (the `AddSpellRedirectPip` precedent), the
  loyalty-chooser call site; cards.json.
* Tests: scenario: Shaman + Doll + Act + {1}{W} pull from a Spirit → Doll dealt 13 + 26.
* Risk: same as step 8.

**Step 10 — Provider.**
* Work: `StuffyDollProvider`, its signature, the hooks of §8, `ExtraLethalDamage`,
  `Certificate() = NotAssessed`; `provider_audit.py --check`.
* Test: a scenario where the depth-0 runner (the greedy policy) finds the Act kill.
* Risk: routing must not catch Minotaur, Slivers, Hinata or Creature Giving (the audit).

**Step 11 — Second main** (`MTG_RFX_SECOND_MAIN`, USER decision Q-U4).
* Work: `DeckUsesSecondMain` detection.
* Test: a scenario where attack-then-Act beats Act-then-nothing.
* Risk: deck-local.

**Step 12 — Optional: the `ProvenWinlessThisTurn` certificate** (§8).
* Measured as a speed-only change with byte-identical labels.

**Step 13 — Forbidden Orchard** (only if the USER asks for the faithful model; §12).

**Step 14 — Only if the USER rejects the deferral: Reckoner's first strike.**
* `temp_first_strike` + an activation mode. Inert, so it would add branching for nothing.

Then Stage 3 onward of `analyze-deck`: the deck enters the suite, subject to the 3x Hinata cost gate
— the probe-and-fan and rider variants are the cost risk (R3).

---

## 11. Risks and open questions

### Questions for the USER (surfaced; work proceeds on the stated default)

* **Q-U1 — deferrals (PROVISIONAL until signed off):**
  * Boros Reckoner's {R/W} first strike (not offered; provably outcome-inert);
  * Loran's hexproof (recorded, inert);
  * the en-Kor shields only as a sweeper rider, and the Shaman source = our sweeper (other windows
    dominated);
  * the canonical trigger order (§4.7).
* **Q-U2 — Forbidden Orchard (§12).** Keep the existing turn-start Spirit model (default), or build
  the tap-time model? The second moves Hinata and Creature Giving play.
* **Q-U3 — spell scry.** Loran's scry 1 would use the provider pick (today's engine-wide treatment
  of `cast_scry`), not a searched keep/bottom. Searching it doubles every plan with a Loran's in it.
  Default: provider pick, as every other cast_scry card does. This is a standing in-window
  heuristic the user may want purged.
* **Q-U4 — second main** for this deck. Recommend ON; measured before adoption.
* **Q-U5 — cast-order rules** (e.g. "creatures before the sweeper") are USER-OWNED. None is proposed
  yet; the order is searched.

### Rules question to verify before step 8

* **Q-R1.** When an en-Kor shield's target creature has left the battlefield, is the shield spent?
  Default: not spent. That is conservative; it can only keep more damage on the en-Kor.

### Risks

* **R1 — executor/rollout lockstep.** Every entry point is a shared helper, but the rider travels
  two ways: `CastRiders` (rollout) and `StackEntry` (executor). One missed copy is a silent
  `[fd-diverge]`. Mitigation: `MTG_RFX_VERIFY` + a one-seed same-outcome unit test per step.
* **R2 — plan-signature drift.** Two new plan/rider fields; `check_plan_sig_lockstep.py` must pass.
* **R3 — cost / 3x Hinata.** The probe-and-fan adds one apply per eligible plan; the rider adds
  variants. The singleton pre-check and F4 are what keep it ×1. Measure the deck's ms/game early (after
  step 3, `suite_gate.py --cost`), not first at Stage 4.
* **R4 — Minotaur arming** (Boros Reckoner). Byte-identity is expected, not guaranteed; check
  `play-changed=0` and that no sim-key or EvalCard reader sees the new param.
* **R5 — `EffectiveSpellCostImpl` and `ManaPruneBound`** are on every deck's hot path. Gate the
  reducer on the param and bail the prune only when a candidate carries it.
* **R6 — combat hook.** `ResolveCombatDamage` is shared by all decks; strictly behind `reflect_armed`.
* **R7 — pre-existing divergences (§1.1)** in the rollout's creature-burn and any-target kills
  (`TurnSolver.cpp:73148-73158`, `73041-73062`: no indestructible, no graveyard, no `OnCreatureDies`).
  Not on this deck's paths and not fixed here. Worth a separate ticket: they would bite the first
  deck that burns its own indestructible creature.

### Surprising findings

* **The Orchard's turn-start model is EXACT here, not an over-approximation (§12)** in single-main
  play. This contradicts the ledger's framing that the tap timing is load-bearing and must move.
* **Combat matters for Guilty Conscience** ("deals damage" includes combat). Combat damage must
  therefore enter the pipeline for armed decks, although combat never damages our creatures.
* **Spiteful Sliver is an AMPLIFIER** (k instances), so a reflection aimed at our own Sliver can beat
  the face. That is the one non-loop reason the target axis is genuinely searched.
* **Shaman en-Kor's {1}{W} can steal a sweeper's 13 from an opponent Spirit** and forward it to the
  Doll (+13 per {1}{W}). The ledger does not mention this line.
* **The engine's {0}-repeatable machinery** (`SpendRepeatActivations` stops at a free cost) cannot
  host the en-Kor as an ordinary activation. That is part of why the rider shape is chosen.

---

## 12. Forbidden Orchard: does the Spirit have to appear at the actual tap?

**Today.**
* `SpawnForbiddenOrchardTokensTurnStart` (`SpellEffects.h:26586`): one Spirit per Orchard at turn
  start, executor `GameEngine.cpp:555`, rollout `TurnSolver.cpp:79360`.
* Plus an on-play spawn for a freshly played Orchard.

**What the timing changes in this deck:**
* Blasphemous Act's cost is LOCKED before payment, so a Spirit from an Orchard tapped DURING the
  Act's payment does not discount it.
* An Orchard tapped BEFORE casting, with its mana floating, does.
* Chain Reaction's X counts at resolution, so Spirits from paying for it count either way.

**Claim: in single-main play the turn-start model is exactly the legal line "at the start of main 1,
tap every untapped Orchard for mana and let it float".** That line is weakly dominant in THIS deck.

Why it is exact:
* Floating mana lasts the whole phase (CR 500.4), so every main-1 spend of that mana is still
  possible.
* The colour is chosen at tap time, but plans are deterministic and known, so the right colour is
  always choosable.
* An Orchard played later spawns at play time, which the on-play hook already does.
* A Karoo bouncing a tapped Orchard is legal after tapping it.
* An Orchard whose mana goes unspent still gave its Spirit ("tap and waste" is legal; there is no
  mana burn).

Why it is weakly dominant: an opponent Spirit never hurts this deck in the goldfish. It never blocks
or attacks; it cheapens Blasphemous Act and grows Chain Reaction's X.

**The only divergence is across phases.** The model credits a Spirit to a main-1 sweeper while
letting that Orchard's mana be spent in main 2. With main 2 off (the default today) there is no
divergence. With main 2 on (§7, Q-U4) the leak exists, but needs a main-1 sweeper plus main-2 Orchard
mana, which is a rare and dominated-looking line.

**Options:**
* **O0 (default): keep the model.**
  * Disclose the cross-phase leak if main 2 is enabled.
  * Cost: none. Hinata and Creature Giving are unchanged.
* **O1: faithful tap-time Spirits.**
  * Record an Orchard tap as a new `mana_tap_mark` bit, the `DamageEvents.h` model step 1. Every
    payment snapshot and rollback already carries the mark.
  * Flush it into a Spirit at payment commit.
  * Add a sound "pre-float untapped Orchards before a creature-count-cost spell" step, inside the
    mana-payment exemption: weakly dominant within one phase, as argued above.
  * Cost: ManaPayment changes (the 12.5K-line hot file), payment-rollback correctness, and **GT
    moves for Hinata and Creature Giving** (fewer Spirits on turns they never tap an Orchard).
    That is a play change across three decks needing a held-out A/B.
  * Estimate: one to two implementer sessions plus the A/B.
* **O2 (cheap partial): only when main 2 is enabled, end an Orchard's availability at the end of
  main 1** on turns its turn-start Spirit was already read by a creature count. This closes the only
  leak without touching payment. It needs a per-Orchard "spent-in-main-1" bit and the floating-mana
  reset verified at the phase boundary.

**Recommendation:** O0 now, O2 if the user turns main 2 on, and O1 only if the user wants the shared
card made literal for every Orchard deck.

---

## 13. Handoff

Design complete. No code written, nothing built, nothing committed. Scryfall rulings for all twelve
cards are in `build/scratch/stuffy/rulings/` (build volume; regenerable from each card's
`rulings_uri`). The next action is step 1 of §10.

---

## 14. Implementation notes (steps 1-3, 2026-11-17)

Where the code departs from the sections above, and why. Each entry names the symbol.

### Step 1 (pipeline core)

* **Graveyard: the OWNER's, not the controller's** (`dmgrx::SbaPass`). Section 2.2 step 5 said
  "controller's graveyard (matching `GameEngine.cpp:1710`)". CR 400.3 sends a card to its owner's
  graveyard, and the legacy `PerformDamageAllCreatures` already did that. Lockstep does not need the
  executor's rule here: the pipeline kills inline, so the executor's `CheckStateBasedActions` never
  sees these deaths. Side finding: the executor SBA (and `CounterPingKillAt`) push to the
  CONTROLLER's graveyard. Only a stolen creature can observe the difference. Not fixed here.
* **The pending stack is a thread-local frame** (`dmgrx::t_frame`), not a local vector. A nested
  `DealDamageBatch` on the same state (a trigger resolving) only pushes. The top-level call drains.
  `MTG_RFX_VERIFY` asserts at the three dmgev decision-boundary backstops (`GameEngine::MainPhase`,
  `ApplyPlanDirect`, the pain sweep) that no frame is open and nothing is pending.
* **The arming param arrives in step 1**: `CardParams::reflect_dealt_damage` (enum `ReflectTo`) and
  the `StampDeckTraits` scan, so "stamping" has something to stamp. No card carried it in step 1.
* **Life SBA**: `dmgev::SbaCheckpoint` runs only on `own_death_live` decks (it parks our life at
  `kLostLife`). "Game over" for the trigger stack is `OpponentLostRaw || (own_death_live && our life
  <= 0)`. The Stuffy Doll deck is NOT `own_death_live` today. Step 4 (a Doll can choose us) should
  arm it from a Doll-only param, not from `reflect_armed`: arming it from `reflect_armed` would move
  Minotaur.
* **Disclosed limits** (header note): damage-source keywords (lifelink, deathtouch, infect) are not
  modelled, because no source in the pool has one. On a board that also arms dmgev, a noncreature
  sweeper's CREATURE damage raises no Tamanoa trigger. Animated lands are not dealt sweep damage (the
  legacy sweep's and the executor SBA's own `card.IsCreature()` predicate).
* **Routing today is a MISROUTE**: the decklist alone sends Stuffy Doll to `ModernRwBurnProvider`
  (Arid Mesa's `fetch_land_types` sets `anti`, the name "Inspiring Vantage" sets `modern_rw_burn`).
  Section 8's "most likely routes to Generic" is wrong. The routing-table unit test
  (`test_pirates_provider.cpp`) pins the measured value; step 10 must route above that chain.

### Step 2 (reflectors, sweepers)

* **Nemesis's lock lives on GameState, not Player**: `GameState::cant_gain_life_rest_of_game_mask`
  (bit p = players[p]). It sits in the 1-byte hole after `damage_cant_be_prevented_this_turn`.
  `Player` has no padding, and a Player bool would have grown both structs by 8/16 bytes for every
  deck. It is folded into `dominance::Build` and `BuildSimKey` only when nonzero. `GainLife` and
  `OpponentGainsLife` honour it.
* **F1 is an ENGINE fold, not only a provider one** (`dmgrx::OwnFaceDominated`). Without it the
  step-2 tripwire fires on every reflection after a sweeper, since {opponent, us} is never a
  singleton, and the step-2 scenarios could not run. The proof is in the code comment: no own-life
  reader in the engine rewards lower life, and Purity / a noncreature-source Tamanoa keep the option.
  It is lifted under human play and `MTG_UNPRUNE=reflecttarget` (new `UnprunedGate::ReflectTarget`).
  F2/F3 and the Spirit restriction are NOT in the engine. A death or damage payoff (Blood Artist,
  Massacre Wurm) breaks F2's proof in general, so they belong to the deck provider (step 10).
* **Planeswalkers are not offered as reflection targets.** This is a disclosed PARTIAL on the three
  cards. The goldfish opponent never controls one, and damage to our own only removes loyalty.
* **The tripwire** (`dmgrx::ResolveTrig`): a target list of size > 1 while
  `!HumanPlayActive() && greedywindow::InWindow(0)` calls `greedywindow::Trip`. The front is taken
  for a singleton, beyond the horizon, in the d0 runner, by the executor, and in human play. The
  viewer prompt is step 3.
* **The Act's same-turn discount** reuses the affinity machinery. `CostFallsAsCreaturesEnter(params)`
  (`CardDatabase.h`) replaces the bare `affinity_for_subtype` test at every scalar-bound bail and gate
  site. `AffinityFeedCount` counts every creature a cast puts onto the battlefield for the Act.
  `SameTurnAffinityDiscountFor` and the prepay line credit admit it. The credit stays rank-filtered,
  as for affinity: on the generic order creatures (10) precede the Act (20).
* **Chain Reaction's X** is `SweepDamageAmount` (`SpellEffects.h`). Both dispatch sites read it at
  resolution: `CreatureCount(0) + CreatureCount(1)`, animated lands included as the count's
  convention.

### Step 3 (searched reflection-target axis)

* **Pins index CHOICES, not triggers** (`dmgrx::t_pins` / `t_pin_cursor`, `Plan::reflect_targets`). A
  trigger whose list (after F1 and the provider) is a singleton takes no ordinal. Section 4.1 says
  "pins[cursor++]" per trigger, as Incinerator does. Counting only choices means a cascade with no choice
  carries no pin and costs no probe beyond the first, and the vector stays short. Probe, apply and
  executor all use the same rule, so the ordinals agree.
* **No in-window tripwire remains.** An ordinal with no legal pin takes the list's front, exactly as
  `FlushIncineratorDamageTriggers` does. The step-2 `greedywindow::Trip` cannot stay. Many speculative
  applies run inside the window before the fan has pinned anything: other axes' probes, this axis's own
  probe, breakpoint discovery. Each of those would abort. What guarantees "no in-window pick" is the fan:
  every plan `AppendSubdecisionAxes` emits carries a pin for every choice its apply reaches.
* **The per-plan pre-check is generic, not section 4.1's "no GC and ≤ 1 Spiteful".** That pre-check
  assumes the step-10 provider folds (F2/F3, the Spirit prune). Without them the generic list after
  any surviving creature is never a singleton. The engine instead skips the probe for a plan with no
  action that can damage a creature (`dmgrx::DefCanDamageCreatures`), or with no reflector whose target can
  be a choice on the board or in the plan (`dmgrx::DefReflectsWithChoice`; Mogg Maniac is always a
  singleton). That is sound because `DefCanDamageCreatures` lists every route into `DealDamageBatch`
  today (the `damage_all_creatures` sweeps, Chain Reaction's counted X, Rolling Earthquake, Pyrohemia).
  **Steps 4-5 must extend it** (the Doll's self-ping, GC, combat). This also keeps Minotaur's armed Boros
  Reckoner probe-free, since that deck has no sweeper.
* **Breakpoint key / fold sites.** The field is folded into `BpCandFingerprint`, `IsApplyEmptyPlan`, the
  base-plan predicate beside it, `PlanIsAxisVariantExceptOrder`, `SamePlan`, `PlanDomCensus` (with
  `kPlanDomAssertedSize` 744 → 768), `ApproxPlanBytes`, the candidate-share bucket hash, the plan hash
  (where only Keranos was folded), the plan diff string, the JSON dump, `ContPins`/`ContPinsOf`/
  `ContPinScope`, the bp capture/resume cursor (`pin_reflect_cursor`), and `BuildBreakpointKey` (only
  while pins are installed and non-empty, as for Incinerator). `test/check_plan_sig_lockstep.py` reads
  action fields only, so a plan pin does not touch it. It already exits 1 on the base commit
  (`#BB` and `crackle_targets`/`splice_count` fast-only), unrelated to this step.
* **`sizeof(Plan)` grows by 24 bytes for every deck.** No hole fits a vector. Tier-1 admission uses the
  portable constant (`tier1::kPlanB`), so it does not move. The byte-budgeted plan caches are
  result-neutral by contract.
* **Provider hook hardening.** The engine drops ids the hook added. A hook that empties a non-empty list
  falls back to the engine's list, because a mandatory trigger with a legal target must target (CR 603.3d).
  Under human play the hook is not consulted, so the viewer always shows the full legal set.
* **Viewer.** At a choice, `ResolveTrig` calls `g_play_target_chooser` with the reflector's def. It
  exposes the engine's own legal list through `dmgrx::t_viewer_legal`, and main.cpp's `target` builder
  renders it (faces, then creatures with P/T). Mogg Maniac and singletons are not prompted. A
  `tools/play/DECISIONS.md` row and the `reflect_dealt_damage → target` auditor mapping were added.
  Stage 5h still owes a forced-type scenario run through the viewer.
* **The design's "two Spiteful Slivers + Reckoner, a Sliver beats the face" scenario moves to step 6.**
  Spiteful Sliver does not exist until then. With step-3 cards, F2 holds: every non-face target has
  mult = 1, so no fixture can show a non-front target winning strictly. Step 3 tests that the axis
  BRANCHES and replays instead: a unit fan test, a unit executor-lockstep test, and the
  `stuffy_chain_reckoner_survives_target_fan.json` scenario, which was the old tripwire case.

### Step 4 (Stuffy Doll)

* **The chosen-player fold is DECK-level, not board-level** (`dmgrx::ChosenPlayerFoldHolds`). Section 4.6
  says "the same predicate as F1", which is a BOARD test (Purity / Tamanoa on the battlefield now). The
  Doll's choice is locked for the object's lifetime, so a board test at ETB is not sound: a Purity cast a
  turn later would make "us" the better choice after the fact. The fold therefore holds iff
  `!GameState::dmg_events_armed` -- the deck-constant superset (main + sideboard) of every card that turns
  damage to us into something -- and not under human play or `MTG_UNPRUNE=reflecttarget`. The rest of the
  proof is F1's (no own-life reader rewards lower life; damage to the opponent is monotone good).
* **Where the fold fails, the ETB CHOICE AXIS fans it** (the Utopia Sprawl / Trufflesnout vehicle, not a
  new pin): `CastCarriesEtbChoice` admits `etb_choose_player`, the post-pass skips the fan while the fold
  holds, and the base `EtbChoiceCandidates` returns `{1 = opponent, 2 = us}`. The pin reaches
  `FireOwnEtbTriggersBody` as `etb_kx`. No current deck pairs a Doll with a dmgev arming card, so the fan
  is live machinery that never fires today (a unit test drives it with `dmg_events_armed` set). An
  UNPINNED entry takes the opponent -- under the fold the dominant choice; otherwise only where the fan does
  not score (beyond the horizon, the d0 runner, a speculative apply) or on a put (no route puts a Doll).
* **The reflection's recipient is read at the SNAPSHOT** (`PendingTrig::fixed_player`, LKI), so a Doll
  killed by the same event still hits its chosen player. `LegalReflectTargets` returns the singleton, and
  `ResolveTrig` neither consults the provider hook nor prompts for `ChosenPlayer`. A Doll staged without the
  as-enters replacement (`chosen_player` -1) takes the opponent; the scenario loader stages one with an
  explicit `"chosen_player": "opponent" | "you"` (default opponent).
* **`own_death_live` is armed from `etb_choose_player`** (GoldFishRunner), never from `reflect_armed`, so
  Minotaur does not move. A human may choose themself, and then the Doll can kill us (the ruling's SBA order
  is step 1's pipeline order).
* **SelfPing** is the `{T}`-only Wellwisher shape: `self_ping_damage` plus a loader-DERIVED empty
  `self_ping_cost`, appended LAST to the enum, the three ModeSpec tables and the plan-dump name table; added
  to `ConvokeBodyTapAbilityLive`, `BpActivationAbilityUnambiguous`, `NumberedTapSource`, the untap-target
  "can get tapped" list and `DefCanDamageCreatures`. Resolution `dmgrx::SelfPingAt` is one pipeline event.
  F5 (ping weakly dominates not pinging in the last main's trailing pass) is NOT applied here: both twins
  are searched; the prune belongs to the step-10 provider.
* **`Permanent::chosen_player`** (int8, -1) sits in the pad byte after `chosen_keyword_mask` (sizeof stays
  328). Folded, gated on `>= 0`, into `dominance::Build`, `BuildSimKey`'s per-permanent mix, the turn key,
  the transposition key, the permanent equality, the removal fold key, the provider state key, and both
  plain-for-fold predicates (a Doll with a choice is not plain).
* **Viewer.** The as-enters choice is a players-only `target` prompt flagged `choose_player` (no
  `per_target_damage`; its own GUI branch); the activation has the line verb `selfping=<source>` (two Dolls
  in the list: the `taplife=` reason) through `LineSpec::self_pings` and every CheckLine site. The auditor
  maps `etb_choose_player -> target`, `self_ping_damage -> main_phase` (+ `verb:selfping`), and no longer
  expects a reflection prompt for `chosen_player`.

### Step 5 (Guilty Conscience + loops)

* **The step-4 dropped check, root-caused: there was no "don't ping" plan in main 1.** The main-1 subset
  walk skips the empty selection (`if (sel.empty()) { continue; }`, TurnSolver.cpp, both walks), and the
  hosts append the pass only when the enumeration is EMPTY (FSLineWin's idle node, SolveWithLookahead's
  empty candidate set) or `FaceUpHoldPassMissing` holds. So with the drop spent and a settled Doll the only
  main-1 line was the ping: "keep the Doll untapped through combat" was unreachable. That is an in-window
  removal of the "don't activate" option with no stated fold, and ping-now does NOT dominate ping-later
  once main 2 can change the board (a GC cast after combat makes the later ping a loop). Fix:
  `SelfPingHoldPending` joins `FaceUpHoldPassMissing`'s predicate (reflect_armed, an untapped self_ping
  permanent of ours that can tap now, not in a breakpoint enumeration / narrow rollout / human play), so
  the hosts append the pass, exactly the face-up hold precedent. No other deck carries `self_ping_damage`.
  Test hook `TurnSolver::HoldPassAppendedForTest`; the step-4 sickness test now asserts it.
  **Engine-wide observation, NOT changed (surfaced to the USER):** every deck's main 1 with any plan offers
  no "do nothing" line once the land drop is spent (main 2 always appends its pass). Where combat can give
  main 2 something new (combat-damage draws/mana), "hold everything for main 2" is unreachable.
* **The GC snapshot is taken after APPLY, before SBA** (`dmgrx::ApplyOne` step 3b), not with the
  reflection snapshot before apply: the per-source totals need the player damage actually DEALT
  (`ApplyDamageToPlayer`'s return), and before SBA every GC is still attached to the host that dealt the
  damage. Equivalent to section 2.2 step 3 (nothing between apply and SBA moves an Aura). GC triggers are
  pushed after the reflection triggers (section 4.7's canonical order). A GC trigger is a `PendingTrig`
  with `aura_host != 0` (kind None, src = the GC by LKI); `ResolveTrig` deals that much to the host, or
  nothing if it is gone.
* **Combat** (`dmgrx::CombatDamageDealt`, Combat.cpp, both damage paths, `reflect_armed` only): the legacy
  body still applies the step's damage to the player; the hook runs LAST in each step (after every
  positional hook, so a host's death cannot shift an index another hook reads) and raises only the GC
  triggers, since goldfish combat damage never reaches a creature. Disclosed: `CombatDamageResult::
  total_damage` does not include the loop's life loss (projection only); under the non-default
  `MTG_COMBAT_STABLE_SOURCES=0` a first-strike GC host that dies leaves a stale regular-step index.
* **GC's host is SEARCHED, not ranked.** `LegalEnchantTargets` emits one variant per host, but two engine
  shapes collapsed them: the AURA CAST HOST RANKING (USER 2026-10-06; the autonomous plan signature keys a
  creature Aura by NAME and the dedup keeps the best-keyed host) and the late-bind / greedy host collapses
  (`CreatureAuraHostInert`). GC is the Curiosity case: its host IS its effect. `CreatureAuraHostInert` now
  excludes `aura_host_damage_back`, and both signature builders key `#A<host>` for it, so every host is its
  own plan. The viewer already asks the host (the `is_aura` enchant sub); `DECISIONS.md` notes it.
* **Termination diagnostics**: `dmgrx::t_last_drain` (resolutions, stop reason) records how the last
  top-level drain ended. Doll + GC + ping from L ends by GameOver after <= 2L+1 resolutions; a constructed
  zero-progress loop (indestructible Reckoner + GC, every reflection pinned at itself) ends by the
  no-progress rule after exactly 64.
* **Section 10's expected numbers, corrected.** "GC on an attacking Nemesis -> 3 + 3 + 3" is 3 + 3: GC's 3
  kills the 3/3, GC goes to the graveyard with it (section 2.4), and only the LKI reflection follows.
  "GC on a non-indestructible Reckoner hit by 3 -> 2x3" holds for a Reckoner that DEALS 3 (combat); one
  that is dealt 3 dies at once and reflects 3 once.
* **OPEN GAP (no-greedy), not fixed in step 5:** a reflection-target CHOICE raised INSIDE COMBAT (GC on an
  attacking Boros Reckoner / Screaming Nemesis: GC hits the host, whose reflection may target the face or
  any creature) is UNPINNED and takes the list's front (the opponent's face). The step-3 fan probes the
  main-phase apply only; combat runs after it in both worlds, outside `PinScope`. With four GCs the face is
  not always best (Reckoner+GC attacks, its reflection aimed at a GC'd Doll loops). Fix shape: carry the
  plan's reflect pins (cursor included) through `SimulateCombat` / the executor's combat and extend the
  probe to run combat, or a combat-crossing pin like the other combat axes. Disclosed in GC's oracle note.
  **CLOSED by step 5b** (below).

### Step 5b (combat-time reflection pins)

* **Shape: a combat suffix on the existing pin vector, carried to combat on the STATE** (the Wanderwine Prophets
  `scripted_prophets_sac` route, the precedent for a choice consumed by a combat-damage trigger). `Plan::
  reflect_targets` is `[main-phase pins] [dmgrx::kCombatPinMark combat pins]`. Packing into the one vector keeps
  `sizeof(Plan)` and every plan fold site of step 3 (hash, SamePlan, census, dump, ContPins, bp key...) unchanged,
  since each already folds the whole vector. `dmgrx::PinScope` exposes only the prefix (`t_pins_n`), so the
  main-phase apply never reads a combat pin. `WritePostApplyStatePins` copies the suffix onto
  `GameState::scripted_combat_reflect`; `dmgrx::CombatDamageDealt` (both combat paths, so the rollout's
  `SimulateCombat` and GameEngine's real combat alike) installs it for the drain and ERASES the consumed pins, so a
  first-strike step, the regular step and an additional combat continue one ordinal stream.
* **The combat fan** (AppendSubdecisionAxes, right after the main-phase fan): for each pre-combat top-level plan
  (`g_bp_enum_depth == 0`, not human play) it applies the plan and runs `SimulateCombat` (under the plan's
  `EotHoldCombatScope`, exactly as the scorer does) with a probe registered for THAT state only
  (`dmgrx::CombatProbeScope`; a nested copy's combat cannot add choices), and clones the plan once per admitted
  option at the first unpinned combat ordinal, until every combat choice is pinned. Pre-check (sound, not a
  fold): a GC on the battlefield or in the plan AND a reflector-with-choice on the battlefield or in the plan
  (`DefReflectsWithChoice`) -- in the goldfish combat damage reaches a creature only through a GC trigger.
  `DefCanDamageCreatures` (the main-phase gate) is unchanged: combat has its own gate.
* **`sizeof(GameState)` 1608 -> 1632** (the vector; no hole fits one). Folded value-gated wherever
  `scripted_prophets_sac` is (dominance `Build`, `BuildSimKeyBase`, the plan-cache key, the debug fingerprint),
  cleared at both turn starts, and checked by the executor's re-solve realisation diagnostic
  (`CarriesPostApplyPin` / `RecordReSolveRealized`).
* **Residuals (disclosed).** (1) A breakpoint continuation's combat is its outer plan's: the outer probe applies
  the plan with its own breakpoint pins, but a wave-walker compound clone (`bp_pre`) inherits its source's combat
  suffix; if its combat differs, an illegal pin takes the front (the Prophets residual). (2) Main 2's additional
  combats (Moraug) are not probed (no such card in this deck). (3) Viewer: a combat choice is the existing
  reflection `target` prompt (same resolver), noted in DECISIONS.md.
* **Audit.** `audit_viewer_decisions.py --no-sweep` now passes: `cost_less_per_creature_on_battlefield`,
  `damage_all_creatures_count_creatures`, `reflect_player_cant_gain_life` classified INERT (no choice), surfaced
  to the USER for sign-off.

### Step 6 (Spiteful Sliver)

* **The grant is a SNAPSHOT count, no new state.** `CardParams::grants_reflect_to_subtype` (the `ReflectTo`
  enum, JSON strings shared with `reflect_dealt_damage` through one reader; `chosen_player` is refused by the
  loader) + the existing `subtypes_affected`. `dmgrx::DealDamageBatch` step 3 reads `dmgrx::Granters(s)` once
  per event, before apply and SBA, and raises, for every damaged creature, its own reflection first and then
  ONE `PendingTrig` per granter that covers it (`GrantCovers`: same controller, one of the listed subtypes, an
  all-types animation counts), in the granters' battlefield order. CR 113.2c (each instance of an ability
  functions independently; two Spiteful Slivers = two triggers per Sliver). A granter killed by the same event
  still grants for that event (LKI, section 2.4); after it leaves, nothing is granted. No `GameState` /
  `Permanent` / `Plan` field: every plan-fold site of steps 3/5b is untouched.
* **Arming / gates.** `rfx_arms` (GoldFishRunner) adds the param. `DefReflectsWithChoice` is true for a granter
  of a choice kind, so the main-phase and combat fans' pre-checks see it; testing the GRANTER suffices, since a
  granted trigger exists only while one is on the battlefield (or cast by the plan). `DefCanDamageCreatures` is
  unchanged: "target player" adds no route into a creature.
* **The searched choice.** The granted "target player" list is {opponent} after F1 (no pin, no probe), and
  {opponent, us} under Purity / a noncreature-source Tamanoa / human play / `MTG_UNPRUNE=reflecttarget`, which
  the existing axis fans. The amplifier case of section 4.1 (a Reckoner's reflection aimed at a Sliver with k = 2
  instances beats the face) is the existing Reckoner "any target" axis: the scenario
  `stuffy_spiteful_reckoner_gc_aims_sliver.json` (combat-time choice via step 5b's fan, win T4 only through the
  Sliver) and a unit test that branches the face and both Slivers and replays each in the executor.
* **Section 10's numbers.** "2 Spiteful + Act -> 4 x 13" holds: `stuffy_spiteful_two_act_52.json` (opponent at
  52, exactly lethal). Unit: 2 Spiteful + a Sliver token = 3 x 2 x 13 = 78 (the opponent's Sliver token and our
  Goblin token are not granted); 2 Spiteful + Reckoner = 4 x 13 + 13.
* **Viewer.** The `target` prompt per trigger instance already existed, but main.cpp keyed it on the def's own
  `reflect_dealt_damage`, and a granted reflection resolves with the damaged Sliver's def (Spiteful Sliver's own
  is None). New `dmgrx::t_viewer_reflection` (set by `ResolveTrig`, cleared by `EtbChoosePlayer`) is the key now.
  Unit-tested under Purity. DECISIONS.md row extended; auditor maps `grants_reflect_to_subtype -> target`.
  **Disclosed gap:** a granted reflection on a Sliver TOKEN (no `CardDefinition`) is not prompted -- the
  resolver takes the front (the opponent). No card in the pool makes a Sliver token.
* **Planeswalkers** stay unoffered (the step-2 PARTIAL, PROVISIONAL), stated in the card's oracle note.

### Step 7 (Loran's Escape)

* **The oracle has no "you control".** Verified against Scryfall: "Target artifact or creature gains hexproof and
  indestructible until end of turn. Scry 1." The legal set is EVERY artifact or creature, either side
  (`trick_targets_artifact_or_creature`, `SpellEffects.h ArtifactOrCreatureTrickTargetLegal`), not "a creature you
  control" as earlier drafts said. New params `grants_temp_indestructible` (sets `Permanent::temp_indestructible`,
  which `HasKeyword(Indestructible)` and dmgrx's SBA pass already honour) and `grants_temp_hexproof` (recorded,
  inert: no hexproof state exists). **Hexproof proxy:** an opponent permanent with `temp_indestructible` is refused
  as a target -- the Escape is the only grant source in the engine, so that bit means "hexproof from an earlier
  Escape". Disclosed in the card's oracle note.
* **The target is SEARCHED, with two folds** (CollectActions' `trick_targets_artifact_or_creature` branch): (1) an
  IDENTITY fold over own battlefield targets (`CastTriggerTargetFoldKey ^ temp_indestructible`) and over opponent
  targets (identical Spirit tokens = one); (2) a NO-EFFECT fold (`IndestructibleGrantTargetInert`): a target that
  is already indestructible (the Doll; watcher / copy-magnet / Heroism guards) or a noncreature, non-animatable
  artifact while no `cast_trigger_destroy_target_permanents` card is in hand/library -- all such targets give the
  same scry-only line, so ONE representative is emitted (own battlefield > opponent > hand). Same-plan hand
  creatures/artifacts are targets too (`trick_hand_target`, deduped by name). The provider's
  `TrickTargetCandidates` is deliberately not consulted (the step-10 hook).
* **Q-U3 default taken (pending the USER): the scry is the engine-wide provider pick** -- `ApplyTrickPayload`
  routes `cast_scry` through `ScryTop` (the viewer's top chooser asks; autonomous play takes the provider pick),
  i.e. an in-window single pick exactly like every other `cast_scry` card. Not searched until the USER rules.
* **The relative ORDER of a sweeper and its setup cast is SEARCHED (design 4.8).** Generic `CastOrderRank` ties
  Loran's / Guilty Conscience with Blasphemous Act / Chain Reaction (all noncreature), so the canonical route
  cast them in HAND order and, with the Act first in hand, "Escape (or GC) before the Act" was unreachable at any
  budget. The ordering expansion of the plan enumerator now expands -- following the `loam_order` precedent --
  every plan that casts BOTH a `grants_temp_indestructible` / `aura_host_damage_back` spell AND a creature sweeper
  (`damage_all_creatures`, `damage_all_creatures_count_creatures`, `x_damage_each_creature_and_player`): every
  ordering branched (k! <= 120), deduped by end state, `searched_order` realised verbatim by both apply worlds;
  every other plan passes through in place with the early return's treatment. Under the human saturated-order
  collapse only these plans are still expanded (the order decides what survives, not entry order). An ordering
  that casts a trick before the same-plan hand card it targets is skipped (`OrderingPlacesTrickBeforeHandTarget`:
  the trick would have no target; sound). This is a WIDENING; no cast-order rank changed (ranks stay USER-OWNED).
  For a battlefield target, Escape-before-sweep weakly dominates after, but it is not a pure dominance (the mana
  each order leaves, other casts' positions), hence the searched expansion rather than a fold. Inert for every
  deck without one of the two setup params. Tests: "the Escape before the Act is a reachable plan" and "Guilty
  Conscience on the Doll BEFORE the Act is a reachable plan" (both orders present; GC-first loops to the kill,
  GC-after = one 13; executor matches the rollout); scenario `stuffy_loran_gc_reckoner_act_loop.json` (Act FIRST in
  hand) wins T4.
* **R7 not reachable from this deck.** The two rollout-only kill paths (ApplyPlanDirect's human any-target
  DirectDamage branch and the creature-burn branch) are reached only by DirectDamage-template spells; the Stuffy
  list has none and Loran's adds no damage route. Not fixed.
* **Pre-existing fold gap (reported, not fixed): the identical-target key ignores EOT indestructible.**
  `LandfallTargetFoldKey` does not mix `temp_indestructible`, so two otherwise identical creatures, one of them
  indestructible until end of turn, fold together. Reachable today only for a creature entering after a Boros Charm
  (the Charm grants all own permanents at once). Loran's own branch mixes the bit itself.
* **Unit-test notes.** `GameState::deck_has_spell_target_watch` defaults TRUE (unstamped-conservative), so a
  hand-built test board must stamp it false or the no-effect fold correctly refuses to fold the Doll; an untapped
  Mind Stone is a sac-draw DIG source whose dig twins the test executor does not realise.

### Step 8 (Nomads en-Kor shields)

* **The oracle, verified against Scryfall** (`build/scratch/stuffy/scry/`): Nomads en-Kor {W}, Creature -- Kor Nomad
  Soldier 1/1 (not "Human Nomad"); Shaman en-Kor {1}{W} Kor Cleric Shaman 1/2. Both carry "{0}: The next 1 damage that
  would be dealt to this creature this turn is dealt to target creature you control instead." One shared param,
  `redirect_shield_cost` (only `"{0}"` is accepted by the loader: the rider has no bill), arms `reflect_armed`.
  Shaman en-Kor is added with the shield only; its `{1}{W}` pull is step 9 (disclosed in its oracle note).
* **No `GameState::redirect_shields` list: the shields ride the EVENT** (`dmgrx::Batch::shields`, `dmgrx::Shield`).
  Section 2.3 planned a per-turn list cleared at the three cleanup sites and folded into the keys. Every shield the
  engine or the viewer creates is created for the sweep it rides and allocates at most the damage the en-Kor is
  about to be dealt, to creatures on the battlefield at that moment, so it is always spent inside that event and a
  per-turn list would always be empty again before anything reads it. Hence no new GameState field, no cleanup
  sites, no sim-key / dominance fold, and `sizeof(GameState)` is unchanged. A persistent list becomes necessary only
  with an activation window outside the sweeper rider (USER Q-U1).
* **The rider is a PIN, not an Action field** (`dmgrx::AddShieldRider`, called by `dmgrx::Sweep` in both worlds).
  Section 10 planned `RedirectRider` on `Action` / `CastRiders` / `StackEntry` and in the plan signatures. The timing
  fold (4.2: nothing happens between our sorcery-speed cast and its resolution) makes "activate in response" the
  same as "activate as it resolves", so the allocation is made AT the sweep as a choice on the step-3 pin stream
  (`dmgrx::PinnedChoice`, factored out of `ResolveTrig`): ordinals in resolution order, BEFORE the sweep's own
  reflection choices; fanned by `AppendSubdecisionAxes`' main-phase probe (its pre-check now also counts an en-Kor on
  the battlefield or in the plan as a chooser, `dmgrx::DefOffersShieldChoice`); replayed by the executor from
  `Plan::reflect_targets`. Every plan-fold site of step 3 already folds that vector, so no plan signature, fold
  site or lockstep checker changed. It composes with step 7's sweeper/setup order expansion for free (the pins are
  read at resolution, in whatever order the casts resolve). Option ids: `kShieldPinBase - (dest * 1024 + amount)`.
* **The replacement** (`ApplyOne` step 1): shields apply in list order, one point each, repeated passes until none
  applies (a point moved onto an en-Kor with its own shields moves again; each shield is spent at most once);
  the moved point keeps its source and combat flag (ruling) and then aggregates with the target's own damage into
  ONE "dealt N" (one reflection trigger for the total).
* **Q-R1 (USER question; DEFAULT TAKEN): a shield whose target has left the battlefield is NOT spent.** CR 614.9: "If
  either creature ... is no longer on the battlefield when the damage would be redirected, or is no longer a
  creature ..., the effect does nothing." An effect that does nothing has not replaced the damage, so "the next 1
  damage" it watches for was not used by it (the damage is dealt to the en-Kor normally). With rider-only hosting the
  case cannot arise in play (targets are on the battlefield when the shields are made); it is unit-tested on a
  hand-built batch.
* **The option set** (`dmgrx::ShieldOptions`; block note in DamageReflection.h). F4 as designed, with the TIE fold
  narrowed to what is provably identity: face-only reflectors (every instance can only hit the opponent's face: Mogg
  Maniac, a Doll that chose the opponent, a granted "target player" under F1, no Guilty Conscience attached) fold to
  ONE representative of the highest multiplicity; every other sink reflector class (a target choice -- Reckoner,
  Nemesis --, a GC'd reflector, a Doll that chose us) stays a separate option, identity-folded by name / multiplicity /
  indestructible / chosen player / GC count. Section 4.2's "ties between equal-mult R's -> identity fold" is not
  identity across those (a Reckoner's 26 can be aimed; a Doll's cannot), so they are branched. Without a sink
  reflector: the design's landmarks ({0}; per reflector class all / keep-E-alive / the most R survives) plus "all onto
  one doomed or indestructible NON-reflector, non-en-Kor creature" when that keeps E alive (such sinks are
  interchangeable). The permutation fold over identical en-Kors applies under F4 only (there the lists are
  independent of earlier allocations): a non-decreasing option index.
* **Disclosed limits (landmark bound, as designed).** Autonomous play never redirects onto a LIVE non-reflector
  (another en-Kor included) -- e.g. two Nomads under an Act with no reflector at all cannot save one by feeding the
  other; human play gets the full allocation. Shields are offered only against our own SWEEPER (`dmgrx::Sweep`):
  not against a reflection aimed at an en-Kor (dominated, F2: aim it at the shield's target directly), and not
  against a Guilty Conscience on an en-Kor (not dominated by anything, but needs an en-Kor carrying GC and attacking;
  part of the PROVISIONAL Q-U1 deferral). "Doomed" is predicted with the SBA's own `LethalToughness` before the event.
* **Viewer.** `divide` (main.cpp target-chooser branch keyed on `dmgrx::t_viewer_shield` + the param): the en-Kor
  first ("keep: not redirected"), then every other creature we control, sum = d, default = the engine's allocation;
  `WriteDivideDecisionJson` gained a `note_override`. DECISIONS.md row + note; the auditor maps
  `redirect_shield_cost -> divide` (and `target`, the oracle's "target creature you control").
* **Provider.** Not consulted (no hook); ordering the landmarks is the step-10 provider's.

### Step 9 (Shaman en-Kor's {1}{W} source pull)

* **The oracle, verified against Scryfall** (`build/scratch/stuffy/scry/Shaman_en-Kor.json`): "{1}{W}: The next time a
  source of your choice would deal damage to target creature this turn, that damage is dealt to this creature
  instead." New param `redirect_pull_cost` (the activation's mana cost, `"{1}{W}"`), beside the step-8 shield; it arms
  `reflect_armed` and counts as a chooser for the fan's pre-check (`dmgrx::DefOffersShieldChoice`).
* **A Batch-level kind-1 form, as step 8 did for the shields:** `dmgrx::Pull { on, from, src }` in `Batch::pulls`
  (no GameState list, no key fold: made for, paid at and spent inside the sweep it rides). `ApplyOne` step 1 applies
  the pulls FIRST -- each moves ALL the damage its source deals its target, once -- and then the {0} shields.
* **Order of replacement effects (CR 616.1, 616.1f, 616.2).** The affected object's controller chooses which
  applicable replacement applies first; after one applies, the ones that now apply are considered. Damage to an
  opponent's Spirit has one applicable effect (the pull; one pull per creature, below); once it is redirected, the
  damage would be dealt to the Shaman, so the Shaman's {0} shields now apply and we (its controller) order them. The
  only real overlap -- a pull on a creature that also carries {0} shields -- is a creature of ours, so the order is
  ours; pull-first loses nothing ("shields move k, the pull takes the rest" = "no shields there, the Shaman's own
  shields move k"). A pull whose Shaman or target has left the battlefield does nothing and is not spent (CR 614.9,
  the Q-R1 default).
* **Hosting and the source** (design 4.3): `dmgrx::AddPullRider`, run by `AddShieldRider` before the shields. The
  source of our choice is the resolving sweeper (CR 609.7a: a spell on the stack is a legal choice) -- the PROVISIONAL
  Q-U1 deferral, now disclosed on the card for the pull too.
* **The choice is SEARCHED** on the step-3 pin stream: per puller, a sequence of `PinnedChoice`s over {stop} + one
  representative per target class until stop (ordinals before the shields'). Option ids `kPullPinBase - from`
  (`-0x20000000`; 0 = stop), disjoint from the shield codes, player ids and `kCombatPinMark`. The FRONT is stop (no
  pull, no mana): the legacy outcome wherever nothing pins. Each creature is pulled at most once across all pullers:
  a second pull on the same target has nothing left to replace (and with rider-only hosting no later damage reaches
  it this turn).
* **Sound folds** (stated in the `AddPullRider` block note): P0 a creature the sweep deals nothing is not offered; P1 our
  own {0}-shield en-Kors (the puller included) are not offered -- their free shields reach the same end state without
  the mana; P2 an indestructible face-only reflector of ours with the highest multiplicity among our creatures (the
  chosen-opponent Doll) is not offered -- its survival cannot change and no destination beats its own reflection;
  P3 identical targets (name, def, controller, token-ness, damage, lethal toughness, power, counters, tapped/sick,
  nothing attached, multiplicity, chosen player, incoming damage) fold to one representative, and a puller's picks
  take a non-decreasing class index (a multiset).
* **Deviation from design 4.3's "T must not be a reflector".** That claim holds only for the max-multiplicity
  reflector, and it ignores survival: pulling a doomed Mogg Maniac's 13 keeps the Maniac alive at the cost of {1}{W},
  a real trade. So only the P2 case is folded; every other reflector (and every opponent creature) stays a target.
  No provider ordering ("Spirits first") exists yet: that is the step-10 provider's.
* **The bill: PAID AT THE SWEEP, both worlds, through `TapForCostDirect`** (the Mihail / Arcanum Wings resolution-time
  shape, lockstep by construction; the executor re-derives its pool per action, so nothing it carries drifts). The
  design planned the Spellskite shape (a cast-bill pip on the Action); that needs a count field on `Action` and every
  dedup / signature / fingerprint site (about thirty for `trick_redirect`), for the same affordability. A pull is
  offered only while a COPY of the state can pay the cumulative bill (every earlier pull of this sweep + this one,
  `EffectiveActivationCost` per puller); the whole bill is then paid at once before the damage. So a plan cannot
  overspend: what the pulls tap is gone for every later action of the plan, in the search and in the executor alike.
  The pull's colours also enter `UnmetActivationColorMask` (LandPlay.cpp), beside every other battlefield activation.
* **Viewer.** One `target` prompt per puller BEFORE the shield `divide`: any number of creatures (either side, every
  creature but the puller, minus those an earlier puller took) up to how many `{1}{W}` the mana affords (`max`), min 0;
  default = the engine's picks. main.cpp's `pull_viewer` branch (keyed on `dmgrx::t_viewer_pull` + the param); the
  Shaman's `divide` then totals its own damage plus what it pulled. DECISIONS.md row + note; the auditor maps
  `redirect_pull_cost -> target`.
* **Disclosed limits.** As step 8: only against our own sweeper. The fold P1 is relative to the FULL shield allocation;
  autonomous shields still offer the step-8 landmarks only.
* **Verification.** Unit 3447/3447 (6 new step-9 cases: card data; Spirit pull -> Doll dealt 39 in one event with the
  {1}{W} tapped; no spare {1}{W} -> no choice, same pin inert; the option set and folds; the pull branched by
  `EnumerateMainPlans` with each plan replayed identically by the executor, lands included; the viewer prompt).
  Scenarios 576/576, incl. `stuffy_shaman_pull_spirit_kill` (T4 kill from 39) + `_control` (a Nomads in the seat: no
  win by T6). Smoke: 219/219 job digests identical to step 8.

### Step 10 (StuffyDollProvider)

* **Routing.** `StuffyDollProvider : DeckProvider`, routed FIRST in `DetectDecisionProvider` (flag `stuffy`), above
  `redelem` / `morph` and far above `anti && modern_rw_burn`. Signature exactly as section 8: `self_ping_damage > 0 ||
  aura_host_damage_back || redirect_shield || redirect_pull_cost || grants_reflect_to_subtype` -- params no other card in
  cards.json carries; it survives cutting any ONE of Stuffy Doll / Guilty Conscience / Nomads / Shaman / Spiteful Sliver
  (unit test). `test_pirates_provider.cpp`'s pin is now `{"Stuffy Doll", "StuffyDoll"}`. Every other deck's provider
  is unchanged (the routing table test + smoke digests). The deck has no `.profile.json` yet, so `provider_audit.py`
  lists it under NOT AUDITED until the analysis writes one.
* **`ReflectTargetCandidates`: the SOUND folds F2 / F3** (proofs in the definition's block note, DecisionProviders.cpp).
  With the opponent's face in the list: F2a drops an own creature with NO reflection instance and no GC (nothing reaches
  anyone); F2b drops an own creature with ONE instance that can hit only the opponent's face (Maniac, a Doll that chose
  the opponent, a lone Spiteful grant under F1) and no GC -- the same N to the same face, plus its damage; F2c/F3 drops
  an own creature with ONE "any target" instance (Reckoner, itself included) and no GC on it while NO GC is on the
  battlefield (without GC every damage event is a pure tree, so routing N through it is a delay plus its damage). Any GC
  out keeps F2c's candidates searched (a host deals damage back to itself, so routing can change how often it is hit).
  Kept: the faces, own creatures with >= 2 instances, own GC hosts, opponent creatures. The deck has no death payoff and
  nothing else triggers on a creature being dealt damage (shields / pulls ride only our sweeper), which F2a/F2b rely on.
* **The Spirit prune is a HEURISTIC, default OFF** (`MTG_STUFFY_OPP_CREATURE_PRUNE=1` drops opponent creatures). Design
  4.1 calls it PROVISIONAL / Stage 5e; the COLLAPSE rule puts a heuristic narrowing OFF until a quality A/B. Owed: the
  held-out A/B and the USER's call.
* **ORDER:** an own indestructible GC host with a reflection instance (the unbounded loop) moves to the FRONT -- read
  only beyond the horizon and in the d0 runner. Under `MTG_UNPRUNE=reflecttarget` the hook returns the list untouched
  (human play never consults it).
* **`ExtraLethalDamage` (section 5), `HasExtraLethalModel() = true`.** A `casting` def counts as a CAST only while an
  unconsumed copy of it is in hand; a Doll def beyond that is the plan's self-ping ACTIVATION (TurnSolver's
  ActivatePermAbility carries the source def), credited up to the settled untapped Dolls(opp) -- the section-5 sketch
  credited every untapped Doll whether or not the plan pings. kHuge (2^20) for a GC on a Doll(opp) [board or cast]
  with damage reaching it (the plan's ping or a sweeper); else ping count + for a sweeper `d * (reflection instances of
  every own creature on board + cast, Spiteful grants included)` + `d` per en-Kor + `d` per affordable Shaman pull
  (`(AvailableManaPool - cast mana value)/2`, capped by opponent creatures + untapped Orchards). Chain's d = board +
  cast creatures + untapped Orchards (each tap makes a Spirit); Act's cost at its per-creature discount. Only Solve's
  lethal sum reads it (EnumeratePlans deliberately does not, as before); HasExtraLethalModel also turns off the early
  xwaste test (`!has_extra_lethal`) and reaches the EDF / combo-off gates, inert here (no blink, ComboOffPossible
  false).
* **ENGINE FIX found by the d0 scenario (step-2 machinery): `AffinityFeedCount` credited an ACTIVATION as a creature
  entering.** For a `cost_less_per_creature_on_battlefield` spell it counted every selected action whose def is a
  creature, so the Doll's self-ping (a creature def) lowered the Act's same-turn cost by 1 and "Nemesis + ping + Act"
  read as payable off five lands; the runner cast Nemesis and could not pay the Act. Now only CastFromHand /
  CastFromGraveyard / ActivateVial / BronzePut feed the Act. Only the Act carries the param (no other deck runs it).
* **Hooks NOT added (disclosed):** no `RedirectRiderCandidates` / `ShamanPullCandidates` / Loran's-target / GC-host /
  F5 hook. Each of those choices is BRANCHED by the search (steps 5, 7-9), and its FRONT -- the only thing an order
  changes outside a budget -- is already the section-5 playout policy (shields: the F4 / engine allocation; pulls: stop;
  Loran / GC host: the greedy picker's own subset choice). "Spirits first" / landmark ORDER would only change the
  exploration order under a binding budget; adding a provider surface for that is left for a measured need. F5 stays
  OPEN to the search (both ping twins searched) -- no tested argument for the prune exists yet.
* **Cast order: the GENERIC default** (DeckProvider inherits GenericProvider::CastOrderRank). ModernRwBurnProvider's
  ranking, which the deck rode by accident, is gone. USER question Q-U5 (the deck's cast order) stands.
* **Certificate() = NotAssessed** (the section-8 bound is step 12).
* **ENGINE FIX (step-9 machinery): a Shaman pull on a passive-opponent spawn ABORTED.** `dmgrx::PullCode` refused
  `from >= 2^17`, but the opponent's scheduled spawns are numbered from `GameState::kFirstOppSpawnNumber` (500000), and
  they are pull targets. Found by step 10's d0 control batch (`[rfx] FATAL: Shaman en-Kor pull target 500000 cannot be
  encoded`). The range is now 2^28 (codes down to -0x30000000: still below every shield code, -0x18000000 at the
  lowest, and above `kCombatPinMark`). Shield codes keep 2^17 (their dest is our own creature: cards 1..N, our tokens
  from 1000). Inert for every other deck (no pull source).
* **Verification.** New unit cases (routing + cut-one-card robustness; F2/F3 folds; GC lifts F2c and fronts the GC'd
  Doll; no face -> no fold; ExtraLethalDamage incl. the ping-activation rule; the pull-code range). New scenario
  `stuffy_d0_runner_finds_act_kill` (depth 0: Doll + 4 Spirits, Act vs Nemesis in hand, wins T4; control with the old
  ModernRwBurn routing pinned via MTG_PROVIDER_DECK: no win by T4). Unit 3453/3453, scenarios 577/577. Smoke 219/219 job
  digests identical to step 9 (run before the PullCode range fix, which is inert without a pull source).
  `provider_audit.py --check` rc 0 (Stuffy Doll NOT AUDITED: no profile yet); static viewer audit rc 0. d0 sanity,
  50 games seed 1: 44 won, avg win turn 5.386 (metric avg 5.82), no abort / tripwire; the same games on the old
  ModernRwBurn routing: 44 won, 5.591 (6.00).

### Step 9b (the en-Kor riders on every damage event; the USER's heuristic prunes)

* **The rulings (verbatim, the spec; ledger "Open questions for the USER").**
  * ~20:30: *"My take on the pull is that it should be possible to pull from any source that is damaged, but we
    should heuristically prune it to what you said."*
  * ~20:35 (the {0} shields): *"Yes, they probably should get the same treatment, but realistically there are no other
    sources of damage that hit our creatures in goldfishing."*
  * ~20:40 (GC host): *"Fair enough, the Guilty Conscience is something that can actually happen in practice. That
    said, it's not a good play to put it on the en-Kor. You should always choose something that can keep the loop
    going."* -- *"(heuristically)"*.
  * ~20:50: *"Actually, Doll first only works consistently if it can tap this turn. If not, we might need to consider
    one other critter that reflects damage. Similarily Loren's Escape should target a critter with Guilty Conscience
    on it if such exists."* -- *"The idea here being that summoning sickness is an issue since we need damage to
    start the chain."*
  * ~20:58: *"Yes, and if there is a reflector without summoning sickness and a doll we should potentially try both.
    The doll is better if we need to wait a turn anyway, but the other creature may be able to go off."*
  * ~21:00: *"But we only need to try a maximum of 2 targets."* ~21:01: *"(and often just 1)"*.
  * ~21:05: *"Hmm... Though I guess in rare cases we would win by combat damage and the conscience could be better on
    a higher-power creature. That makes a clear order."*
  * ~21:08: *"In rare cases we could have a lower-power attacker able to attack and a higher-power one with summoning
    sickness. I would choose the lower-power one unless we have a good chance of lethal next turn with the other."*
  * ~21:10: *"(in which case you can search)"* and *"I don't want an order for the most part. We should prune it."*
  * ~21:12: *"I'm saying this is an exception to the prune."*
  * ~21:14 (Loran's Escape): *"Yes, that sounds good regarding Loran's Escape."* Then: *"I would prune Loran's Escape
    even when we have no guilty conscience to one creature or perhaps even not castable. It is essentially a Scry
    1."*; *"I guess you can argue that it could allow a creature to survive a sweeper... Not great, but that is a use.
    In that case highest power (and not stuffy) I suppose."*; *"It should be a creature that reflects damage also."*
* **ENGINE: the riders run on EVERY damage event.** `dmgrx::Batch::window` (`RiderWindow`: Sweep, Reflection,
  AuraHost, SelfPing) names the event; `DealDamageBatch` runs `AddRedirectRider` (pulls, then shields -- the step-8/9
  `AddShieldRider` / `AddPullRider`, now per event and per player with an en-Kor) before applying, for top-level and
  nested events alike, so executor and rollout stay in lockstep by construction. `dmgrx::Sweep` no longer calls the
  rider itself (same point in the ordinal stream: before the sweep's reflection choices). The choices stay on the
  shared pin stream (`PinnedChoice`); a Guilty Conscience trigger in combat reads the step-5b combat pin stream that
  `CombatDamageDealt` installs, and the combat fan's pre-check counts an en-Kor as a chooser when the provider admits
  that window. No `Combat` kind: in the goldfish nothing blocks, so combat damage never reaches a creature; what does
  in combat is the GC trigger (AuraHost). The timing fold generalises (activating in response to an event = as it
  happens; an earlier activation could only be consumed by an earlier event), and P0 now reads "a later event that
  damages it is its own window". Every batch the engine builds has one source, so "the source of your choice" is
  `b.src[0]`. Cheap guard: `AnyRedirector` (an en-Kor on the battlefield) before the batch is copied.
* **PROVIDER (USER-approved HEURISTIC prunes, restrict-only, each lifted by its gate and never consulted under human
  play; the viewer offers every legal option).** New hooks `RedirectWindowAdmitted`, `DamageBackAuraHostCandidates`,
  `IndestructibleGrantTargetCandidates`; new gates `redirectwindow`, `gchost`, `escapetarget`.
  * (a) **Windows:** the autonomous search sees the pulls/shields only at OUR OWN SWEEPER (`window == Sweep`, source
    ours). A refused window chooses nothing, takes no ordinal and pays nothing: the step-9 outcome. The engine's sound
    folds (P0-P3, F4 / landmarks) still shape the admitted windows. The provider's F2a-F3 reflection-target folds
    assumed "nothing else triggers on a creature being dealt damage"; with the window prune LIFTED and an en-Kor of
    ours out, they are now switched off (a shield or pull could carry the damage on).
  * (b) **GC host = a PRUNE, not an order.** Candidates = loop-sustaining creatures among the engine's hosts
    (`ReflectInstances > 0`: Doll -- unless it chose us --, Reckoner, Nemesis, Maniac, a Spiteful-granted Sliver). A
    host has a STARTER this turn when our castable sweeper reaches it (mana count, GC's own mana value added; colours
    not checked -- an optimistic heuristic input), the Doll's {T} (untapped, `CanTapNow`), or an attack (pre-combat,
    `CanAttackFull`). A Doll with a starter -> ONE host. Else the best go-off-now creature (higher power, ties
    battlefield order) and, in the SPLIT case only, the best wait-a-turn host (a Doll on board, else a starter-less
    reflector of STRICTLY higher power) -> TWO, the search decides. Nothing can go off now -> ONE (the Doll, else the
    highest power). No loop-sustaining host -> the engine's list (never emptied). Hooked in CollectActions' aura
    branch after `LegalEnchantTargets`; the engine drops anything added and keeps its list on an empty result.
  * (c) **Loran's Escape target = ONE target.** A creature of ours carrying GC -> it (every such one in the list; an
    indestructible GC'd Doll is the no-effect line, so the engine's no-effect representative stands for it). Else our
    highest-power damage-REFLECTING creature other than the Doll (ties: battlefield order). Else the scry-only /
    no-effect representative. Else (none) the engine's list. Same-plan HAND creatures are not kept (DECISION TAKEN,
    surfaced: a hand target forces its cast into the plan; the USER asked for one target that keeps the Escape
    castable). Hooked after the step-7 folds (the emission is now collected, filtered, then emitted in the same order).
* **Viewer.** The pull `target` and shield `divide` prompts now appear at any damage event in human play (no
  `main.cpp` logic change: the prompts were never sweeper-specific; the pull wording now says "this event's source").
  In a Doll + GC loop with a Shaman and spare `{1}{W}` the pull prompt repeats once per loop step (decline = no
  creature). DECISIONS.md rows/notes updated; GC host and Loran target prompts are unchanged (full legal set).
* **Verification.** 7 new unit cases (the reflection-event shield, generic vs Stuffy provider; a searched Shaman pull in a
  reflection's event, pinned, paid, replayed; COMBAT: GC on an attacking Nomads, shield onto the Doll; the GC-host prune's
  six board shapes; the host prune reaching `EnumerateMainPlans`; the Escape prune's five shapes; the Escape prune reaching
  `EnumerateMainPlans`). Each asserts the restriction and, under its gate, that it is lifted; the step-9b cases were also run
  under `MTG_UNPRUNE=redirectwindow,gchost,escapetarget` (7/7, 325 assertions). Unit 3460/3460, scenarios 577/577, smoke
  219/219 job digests identical to step 10, `provider_audit.py --check` rc 0 (Stuffy Doll NOT AUDITED: no profile yet),
  static viewer audit rc 0.
* **d0 sanity (50 games, seed 1, as step 10):** 44 won, avg win turn 5.386 (metric avg 5.82) -- the same aggregate as step
  10, no abort / tripwire; 11 of 50 games changed play (digest ff108cd1992a1493 vs 829f3e74fb564e5e). Fully attributed:
  under `MTG_UNPRUNE=gchost,escapetarget` all 50 digests equal step 10's (the window prune moves nothing at d0). Two win
  turns moved, net 0: game 15 T6 -> T7 (the Escape prune: lifting `escapetarget` alone restores T6), game 35 T7 -> T6 (the
  GC-host prune: lifting `gchost` alone gives T7). A d0 sanity run, not a measurement: the held-out A/B of the three
  prunes is owed (USER-approved heuristics, so measured and reported, not gated on).
