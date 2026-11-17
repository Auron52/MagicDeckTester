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

## 14. Implementation notes (steps 1-2, 2026-11-17)

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
