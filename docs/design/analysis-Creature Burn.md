# Analysis ledger — Creature Burn

Deck: `decks/Creature Burn/Creature Burn.cod` (60 cards, mono-R: 18 Mountain, 2 Teetering Peaks, 1 Browbeat,
2 Rift Bolt, 1 Flame Javelin, 3 Fireblast, 4 Lightning Bolt, 4 Magma Jet, 2 Price of Progress, 2 Searing Blaze,
4 Ball Lightning, 2 Cinder Pyromancer, 2 Figure of Destiny, 4 Goblin Fireslinger, 2 Hellspark Elemental,
2 Keldon Champion, 1 Keldon Marauders, 2 Spark Elemental, 2 Vulshok Sorcerer).

Started 2026-11-20 on `phase-1-2-deck-analyzer` @ 4f0ee3a94 (12-thread / 10 GB box).

## Stage 1 — coverage

* Missing (8): Browbeat, Flame Javelin, Cinder Pyromancer, Goblin Fireslinger, Keldon Champion,
  Keldon Marauders, Vulshok Sorcerer, Teetering Peaks.
* All other 11 cards `full`; bracket notes on Ball Lightning / Spark Elemental / Hellspark Elemental are the
  existing accepted trample-inert notes (shared with other burn decks).
* No sideboard; no fetchlands.

## Stage 2 — cards

(in progress — per-card research fan-out, 4 agents)

## Stage 2 — Batch 1 — implemented (integrator 1, 2026-11-20)

Cards: Keldon Champion, Keldon Marauders, Flame Javelin, Browbeat. Every clause modelled; Scryfall re-fetched
for each (mana_cost / oracle pasted verbatim).

| Card | Tier | cards.json | C++ |
|---|---|---|---|
| Keldon Champion | 1 | `custom`, `echo_cost {2}{R}{R}`, `etb_damage_any 3`, keywords `[Haste]` | none (echo is the searched echo axis; ETB face damage the existing cascade) |
| Keldon Marauders | 3 | `custom`, `vanishing_counters 2`, `etb_damage_any 1`, `ltb_damage_any 1` | NEW `CardParams::vanishing_counters` / `ltb_damage_any`, `Permanent::time_counters`, `PerformUpkeepVanishing` |
| Flame Javelin | 1 + sound fold | `direct_damage`, `damage 4`, `targeting any` (twobrid parse exists) | NEW `MTG_TWOBRID_COLOUR_FOLD` (default ON) in the CollectActions twobrid post-pass; `MTG_EMIT_PRUNE` exempts twobrid |
| Browbeat | 2 | `draw_spell`, `draw 3`, `opp_may_take_damage_instead_of_draw 5` | NEW param, `BrowbeatOpponentTakesDamage` (policy, ONE place) + `ResolveDamageInsteadOfDraw` (both worlds) |

C++ sites (all gated on the new params / a nonzero time counter, so other decks are untouched):
* **Vanishing** — counters put AS IT ENTERS at the universal enter cascade (`FireEtbWatchers`, beside
  `enters_with_plus_counters`, doubled by `DoublerShift`) and in `ApplyEntersWithOnPut` for simultaneous
  multi-puts (+ the three multi-put shift-detection loops). `PerformUpkeepVanishing` (SpellEffects.h, beside
  `PerformUpkeepFading`): remove one per upkeep, sacrifice through `SacrificePermanentAt` when the LAST comes
  off (Vanishing 2 = sacrificed at the 2nd upkeep); called at BOTH upkeep sites (`GameEngine::UpkeepTail`,
  `TurnSolver::SimulateEndAndStartNextTurn`). `time_counters` folded, NONZERO-gated, into every key that carries
  `fade_counters` (TurnSolver BuildSimKey Mix / signature string / fold-class refusal + tag / equip fold refusal /
  land-class plain test / permanent equality / turn-shape mix / transposition key; Dominance exact-match
  `mfold`; SpellEffects plain-permanent test + fungibility key + `ForEachDedicatedCounterField`;
  DecisionProviders state vector + combo-route key), the viewer badge (`main.cpp` + a `.cnt.time` style), the
  game-log snapshot, and the `--scenario` loader (`time_counters`). **`sizeof(Permanent)` 328 -> 336** (no pad
  hole left; Dominance.h note + assert updated).
* **LTB damage** — `FireLeavesBattlefieldTriggers` (every leave route that calls it, both worlds):
  `DealPermanentAbilityDamage` at the face inside a `NoncombatTriggerScope`.
* **Twobrid fold** — when the turn pool + floating mana is a single colour (no `{C}`, no wild), every own
  permanent (`EffectiveProducesFor`) and every land in hand makes only that colour, no land Aura, and nothing
  reads mana spent (`cast_trigger_untap_pump_target_min_spent`), the fewest-mana variant (ties: most coloured
  pips) dominates and is the only one emitted. Decision space only; human play unfolded. Prevent Damage
  (Beseech) never qualifies (all multi-colour / `{C}` lands).
* **Browbeat** — `ResolveDamageInsteadOfDraw` at the top of `EffectHandler::ResolveDrawSpell` and the rollout's
  `apply_one` DrawSpell branch: taken -> `DealPermanentAbilityDamage` 5 at the face and `n = 0` (the draw
  breakpoints still run, with the hand unchanged, so the worlds stay in lockstep). Held-card count excludes
  STAGED cards (the rollout keeps them in the hand vector). `EvalCard` policy-aware; `ClassifyMainPhase` ->
  `Both`; spectacle credit `else if` (held = true) beside the Devil's; `GoldFishRunner::DeckUsesSecondMain`.
* Scripts: `vanishing` in `MODELED_ELSEWHERE_KEYWORDS`; `vanishing_counters`, `ltb_damage_any`,
  `opp_may_take_damage_instead_of_draw` in the viewer auditor's no-decision manifest.

Tests: `test/unit/test_creature_burn_batch1.cpp` (19 cases: parse; Champion ETB both worlds; Marauders enter
counters + ping both worlds, upkeep sequence via the shared upkeep and via the rollout's turn boundary, no-counter
case, upkeep-lethal leave in the rollout, sac-outlet leave, multi-put pass; Javelin parse, RRR rollout cast, fold
= `{3}` on Mountains (and with a Mountain in hand), full fan with an Ancient Tomb on the board or in hand;
Browbeat parse + second-main, take at 20, empty hand 5 declines / 6 takes, holding 8 declines / 9 takes, staged
not held, 2HG team life x1). Scenarios `cb_flame_javelin_bolt_rrr_lethal` (4 Mountains, Javelin RRR + Bolt =
7, T4 win) and `cb_keldon_marauders_vanishing_upkeep_kill` (the executor's upkeep leave kill, T4, opponent ends on
exactly 0).

**PROVISIONAL (USER-owned, surfaced, not decided by the integrator):**
1. **Browbeat opponent policy A** — take the 5 iff life > 5 + (we hold a card ? 3 : 0) (the Vexing Devil
   ruling's analog). Contrast policy B: take unless life <= 5.
2. **Browbeat opens the second main** (`DeckUsesSecondMain`) — the post-combat cast meets a different
   opponent choice. Its cost (d3/d5 ms/game with and without, `MTG_FORCE_USES_M2`-style) is still to be measured
   once the deck plays.

Known risk carried from the research: Keldon Champion's echo payment created by applying a plan at a
single-main boundary (`BoundaryFan(state, /*with_echo=*/false)`) is not branched -> measure with
`MTG_ECHO_AXIS_STATS=1` once the deck plays.
