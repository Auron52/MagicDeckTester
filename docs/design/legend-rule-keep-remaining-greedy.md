# Legend-rule keep: the entry routes still resolved by a provider pick inside the search window

Status: LARGELY PURGED (Boars LK, 2026-11-13). Written 2026-11-13 by the Boars I5b item 4 integrator as an
assessment; the LK update below records what is now searched and what remains.

## LK update (2026-11-13)

* **One generic representation** covers routes 1-5: `TurnSolver::Plan::legend_keep_turn` (0 unpinned / 1 keep oldest /
  2 keep newest) rides `GameState::scripted_legend_keep` (written by `WritePostApplyStatePins` in both worlds, cleared
  at both turn starts) and is read by `LegendKeepPick` after the name-scoped cast pin. `AppendSubdecisionAxes`'
  LEGEND-KEEP TURN FAN twins every plan whose turn can make a duplicate (`LegendKeepTurnFanLive`). One answer per
  turn (a disclosed limitation: two different un-ridered groups in one turn get both-oldest / both-newest).
* **The Galadriel miss (route 1 was not the route).** The T5 Grey Havens duplicates of seed 9101 (gi1, gi2, gi6) were
  NOT land drops: Galadriel of Lothlorien's "whenever you scry, reveal the top card; a land is put onto the
  battlefield tapped" (`scry_reveal_land_to_battlefield`) put the second Havens. The fan's entry-route list
  (`LegendEntryRouteParams`) had no land-put terms, so no plan of that turn was twinned and the provider decided
  (`pinned=0`). The list now carries every EnterLand route that can find a non-basic land: the scry reveal, Risen
  Reef's look-top, the land tutors to the battlefield, `tutor_to_battlefield`, and a non-basic battlefield land search.
  Second half: the executor (`AIEngine::TakeTurn`) played the committed plan's main-1 land BEFORE writing its
  post-apply pins, while the rollout writes them first; with the legend rule an SBA at the drop the executor read the
  provider's keep. It now writes `scripted_legend_keep` before the drop. After both fixes every Havens event in the
  repro reads `pinned=2` (commits `c58f8b75a`, `aa1450512`).
* **The legend rule is now a state-based action (CR 704.3), not a beginning-of-combat event.** Before LK it ran only
  where a LEGENDARY spell resolved, at a handful of put sites, and at the beginning of combat, so a second Havens
  played in main 2 (or put by a non-legendary resolution, or returned in the end step) kept both copies through the
  opponent's turn: both untapped and both tapped for mana in the next main 1 (seen in Galadriel seed 9101 gi0: T3,
  two untapped non-sick Havens at combat). `LegendRuleSbaCheckpoint` (SpellEffects.h) now runs at the land drop (the
  shared `PlayLandFromHand` / `PlayLandFromLibraryTop`, both worlds), after every stack resolution
  (`GameEngine::ResolveStack`; rollout twin `DepletionSbaAtResolution`'s exit), and at the end of the end step
  (`GameEngine::EndStep` / `SimulateEndAndStartNextTurn`; commit `b48089aa9`). RESIDUAL: a resolution nested inside another resolution
  in the rollout (cascade, a free recast) is checked at the outermost exit, the depletion SBA's disclosed residual.
* **Remaining:** upkeep-only routes (Snow's Marit Lage's Slumber, Emeria) fire before any plan of the turn, so no
  turn pin is written yet; they need a next-upkeep pin (the `scripted_echo` / BoundaryFan precedent). Unobserved in
  the census.

## Background

CR 704.5j: when a player controls two or more legendary permanents with the same name, that player chooses one,
and the rest go to the graveyard. The choice matters whenever the copies differ: tapped state, summoning sickness,
+1/+1 or loyalty counters, damage, a temporary pump, an attached Aura or Equipment, an end-step rider.

Boars I5b item 4 made the keep a SEARCHED choice for the routes that go through a hand cast:

* `LegendKeepPick` (`src/core/SpellEffects.h`, just before `LegendRuleLoser`) is the single decision point, read by
  the provisional loser (`LegendRuleLoser`) and the SBA (`EnforceLegendRule`). Order: the searched pin
  (`g_legend_keep_pin`, set by `LegendKeepPinScope`), then the provider's `LegendKeepIndex`, then (human play) the
  viewer's `legend_keep` prompt when `LegendCopiesInterchangeable` is false.
* The pin is set only on these routes: `AIEngine::CastSpellFromHand` (executor cast), `EffectHandler::Resolve` (the
  stack entry's `legend_keep`), the rollout's `apply_one` (`CastRiders::legend_keep`), and
  `FireAttackPutFromHand` (Ilharg's attack put, `kAttackPutKeepNewBit`). The search twins them in
  `TurnSolver::CollectActions` (LEGEND-KEEP PASS), `AppendSubdecisionAxes` (LEGEND-KEEP FAN for a plan casting two
  copies, and the attack-put fan).
* The pin carries the legend's NAME. A legend rule for a different name (a nested entry inside the cast's
  resolution) is not captured and falls through to the provider.

Everywhere else the provider's `LegendKeepIndex` decides. The base answer is "keep the oldest"
(`DecisionProvider.h`); `CritterLifegainProvider`, `MirrorwingProvider` and `MidrangeBurnProvider` override it. With
search depth remaining, that is a single pick by a heuristic, which the no-greedy rule
(`docs/design/no-greedy-in-search-window.md`) forbids when the copies differ. When the copies are interchangeable
the pick is an identity fold and is sound.

## Census: what real play reaches (2026-11-13)

Instrument: `MTG_TRACE=legend` prints one line per real-resolution legend rule, now with `pinned=` (a searched rider
decided) and `interch=` (`LegendCopiesInterchangeable`). Run: every suite deck with a legendary permanent at 2+
copies, 20 games (Boars 40), seed 9101, `--depth 3 --budget-ms 10 --ignore-play-profile` (Boars with its profile).

| Deck | Legend | Events | Pinned (searched) | Provider pick, copies DIFFER | Provider pick, interchangeable |
|---|---|---|---|---|---|
| Boars | Ilharg, Raggadragga | 2 | 2 | 0 | 0 |
| Angels | Lyra, Archangel of Dawn | 1 | 1 | 0 | 0 |
| Galadriel | Galadriel of Lothlórien | 4 | 4 | 0 | 0 |
| Galadriel | The Grey Havens (land) | 9 | 0 | **2** (old copy tapped, new untapped) | 7 |
| Goblins | Muxus, Goblin Grandee | 2 | 0 | **2** (old tapped + pumped, new entered mid-turn) | 0 |
| Jumbo Cactuar | Surrak and Goreclaw | 2 | 0 | **2** (new copy has a +1/+1 counter) | 0 |
| Unpredictable Cyclone | Emrakul, the Aeons Torn | 19 | 0 | **16** (new copy summoning-sick) | 3 |
| Mirrorwing Dragon | Zada, Hedron Grinder | 1 | 1 | 0 | 0 |
| Snow | Marit Lage's Slumber | 1 | 1 | 0 | 0 |
| Eldrazi Displacer Flicker | Emiel the Blessed | 3 | 3 | 0 | (interchangeable) |
| Breaching Dragonstorm | Maelstrom Wanderer | 40 | 0 | 0 | 40 |
| Landfall | Phylath, World Sculptor | 1 | 0 | 0 | 1 |

All other suite decks with duplicate legends (Auras, Bruna, Critter Lifegain, Dragons, Five Colour, Hinata, Hydras,
Kitty Equipment, Knights, Lifegain Heliod Combo, Melira Pod, Midrange Burn, Minotaur, NivMizzetCombo, Prevent
Damage, Shadowborn Apostle, Soldiers, White Knights) had no legend rule in this sample. Absence in 20 games is not
proof that a route is unreachable.

Four suite decks reach a provider pick with differing copies: **Galadriel, Goblins, Jumbo Cactuar, Unpredictable
Cyclone.**

## The routes, by function

Each route below has no searched rider. The "reached" column is from the census above. Where it says "reachable",
the deck holds the cards and nothing structural stops the line.

### 1. Land play of a legendary land: REACHED with differing copies (Galadriel)
* Functions: the executor's land entry in `AIEngine::TakeTurn` (the `EnforceLegendRule` after `PerformEtbDig`,
  ~AIEngine.cpp:5029) and the rollout twin in `ApplyPlanDirect` (~TurnSolver.cpp:67247).
* Cards: The Grey Havens (x2 in Galadriel; enters untapped, `etb_scry`). Single legendary lands elsewhere (Boseiju,
  Who Endures in Bruna; Three Tree City in Goblins) cannot duplicate.
* Why it matters: the old Havens is often tapped for mana before the second is played. Keep-oldest then keeps the
  TAPPED copy and loses a mana this turn, while keep-newest keeps an untapped one. The provider's pick is the
  dominated option here.
* Fix shape: twin the PlayLand action (legend_keep 1/2) when the land is legendary and a same-name land is on our
  battlefield; pin over the land entry in both worlds. A provable-dominance fold applies: if the old copy is
  untapped and otherwise plain, keep-oldest is never worse for a land (no sickness on lands).

### 2. Free casts nested inside another card's resolution: REACHED (Unpredictable Cyclone; Breaching Dragonstorm)
* Functions: Unpredictable Cyclone's cycling replacement (`cycle_draw_exile_shared_type_freecast`), Breaching
  Dragonstorm's ETB (`etb_exile_free_cast_max_mv`, MV 8 admits Maelstrom Wanderer), cascade
  (`cascade_max_mv`: Call Forth the Tempest, Sakashima's Protege; cascade cannot hit a same-MV copy of itself),
  Creative Technique (`shuffle_reveal_freecast`), ripple from a DIFFERENT spell (Thrumming Stone's ripple; the
  Stones are interchangeable), Maelstrom Archangel (`combat_damage_free_cast`, Five Colour: Jared Carthalion,
  Nicol Bolas, Cosmic Spider-Man at 2 copies each).
* The pin is keyed to the OUTER card's name, so the nested legend falls to the provider.
* Unpredictable Cyclone: 16 of 19 Emrakul legend rules had differing copies, every one "new copy summoning-sick,
  old copy untapped". Keep-oldest is plausibly dominant there (an untapped, non-sick 15/15 against a sick one), so
  the play is likely right, but it is still a provider pick and nothing proves it.
* Fix shape: give the free cast a rider of its own (the free-cast answer already carries pins in some routes), or
  extend the pin to "the next legend rule for name N" set at the free-cast decision.

### 3. Put-onto-the-battlefield effects: REACHED (Goblins, Jumbo Cactuar)
* Functions: `FireCombatDamageCheatIntoPlay` (Goblin Lackey, `combat_damage_puts_subtype_from_hand`: Muxus x3 in
  Goblins); `ResolveToothAndNail`, `ResolveFinaleOfDevastation` and Green Sun's Zenith's put (Jumbo Cactuar: Surrak
  and Goreclaw x2); `ApplyPutFromHand`; `DeployCreatureFromHand` (Vial-style deploys, the provider host at
  DecisionProviders.cpp ~41947); `PerformGenesisWave` (Landfall: Phylath x4); `PerformReplenish`;
  `ApplyPermAbility` (Birthing Pod in Melira Pod: Melira x3); `FireAttackDigAttach` (Kitty Equipment: a second
  Shadowspear attached to the attacker while the first sits on another creature, which is a real difference:
  which body gets +1/+1, trample, lifelink); `DeployLandAuraFromHand` (no legendary land Auras today).
* Goblins: both Muxus events had the old copy tapped and pumped (it attacked) and the new one entering mid-turn,
  unpinned: the Lackey route. Jumbo Cactuar: both Surrak events had the new copy carrying a +1/+1 counter (the
  old Surrak's "each other creature enters with an additional +1/+1 counter"), unpinned: a tutor-put route. Keep-
  oldest throws away the counter, so the provider's pick looks like the dominated one in that state (the new copy
  also has haste from the old one's static ability at entry; whether it keeps haste once the old copy is gone is a
  rules point to check before any fix).
* Fix shape: a rider on the put action (the attack-put's `kAttackPutKeepNewBit` is the template).

### 4. Token copies: reachable (Mirrorwing Dragon)
* Function: `CreateTrickCopyToken` (~SpellEffects.h:28279, `token_copy_of_target`: Twinflame-style copy of Zada,
  Hedron Grinder; x4 in Mirrorwing Dragon). The rule lives in `MirrorwingProvider::LegendKeepIndex`: keep the
  original unless the hasty copy converts this turn into the win. That is an explicit in-window heuristic. The one
  Zada event in the census was a hand cast (pinned), so this route was not observed in 20 games.
* Also `PerformUpkeepSlumber` (Snow: Marit Lage tokens, `upkeep_sac_token_legendary`). Two Marit Lage tokens need two
  Slumbers to trigger across upkeeps; a token from an earlier turn is untapped and non-sick at upkeep, so
  keep-oldest is very likely dominant. Not observed.
* Copy-entrant creatures (`enter_as_copy_of_entrant` / `enter_as_copy_of_creature`, Sakashima's Protege in
  Breaching Dragonstorm): `EffectHandler::Resolve` and `ApplyPlanDirect` (~TurnSolver.cpp:70049). The pin is keyed
  to "Sakashima's Protege", not to the copied legend. All 40 Maelstrom Wanderer events were interchangeable by
  `LegendCopiesInterchangeable`. Note that check compares battlefield state, not which CARD is underneath: keeping
  the Protege copy rather than the real Wanderer changes which card reaches the graveyard. No suite deck reads that
  today.

### 5. Reanimation: reachable in one suite deck (Melira Pod)
* Functions: `ResolveGyReanimate` (Emeria, the Sky Ruin / Abiding Grace in Lifegain Heliod Combo: both return MV 1
  creatures, so neither Heliod (MV 3) nor Ajani (a planeswalker) can return; not reachable there); Reveillark's
  LTB return (`ltb_return_creatures`, power 2 or less: Melira, Sylvok Outcast, power 2, x3 in Melira Pod: reachable
  while another Melira is on the battlefield); `ApplyGraveyardPlayAbility` (lands from the graveyard; no duplicate
  legendary land in those decks).

### 6. Flicker: not reachable as a duplicate source
* `ApplyBlink` (Eldrazi Displacer, Felidar Guardian, Emiel the Blessed). Every modelled blink exiles and returns the
  same permanent at once, so the returning copy is the only one on the battlefield: the legend rule has nothing to
  choose. A delayed ("return at the beginning of the next end step") flicker would make it reachable; none is
  modelled in a suite deck today. The Emiel events in the census were hand casts (pinned).

### 7. Structural gaps on the covered routes
* Plans from builders that bypass `AppendSubdecisionAxes` and cast two copies of one legend: the second copy's keep
  is unpinned.
* With 3+ copies (one on the battlefield and two cast in one plan), the plan-level fan offers only "always oldest" /
  "always newest"; mixed sequences come only from the action-level twins.

## Out of scope by design (permitted playout policy)

Beyond the horizon (rollout leaves, the d0 runner) the keep stays the provider's `LegendKeepIndex`. That is the
permitted greedy and is an agent-chosen default awaiting a USER ruling (recorded in `docs/design/analysis-Boars.md`,
"I5b item 4 done").

## Priority, if this is picked up

1. Land play (Galadriel): the provider's answer is the dominated one in the observed state, and the fix is small.
2. Put routes (Goblin Lackey, the tutor-puts, Kitty Equipment's attack dig-attach): real differences, the
   attack-put bit is a ready template.
3. Nested free casts (Unpredictable Cyclone has the highest event count, probably benign).
4. Token copies and the Mirrorwing override (remove the heuristic once the route is searched).

A sound DOMINANCE fold (a copy that is untapped, non-sick or haste-irrelevant, with no fewer counters and nothing
attached to the other) would keep each of these twins cheap. The USER has not yet approved one; the item 4 cast
twins ship without it.
