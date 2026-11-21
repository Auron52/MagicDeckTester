# Blood Sun pipeline (Blood Sun deck): engine design

Status: DESIGN, 2026-11-20. **Nothing here is implemented yet.**

The deck ledger is `docs/design/analysis-Blood Sun.md`; its STANDING DECISIONS bind every step below.
Authoritative card text is `logs/bloodsun/oracle.md` (Scryfall oracle text + Gatherer rulings, fetched
2026-11-20; the JSONs are on the build volume at `build/scratch/bloodsun/scry/collection.json`).
Line numbers below are hints that drift; anchor on the symbol names. All paths are relative to the
worktree root.

The deck is `decks/Blood Sun/Blood Sun.cod`: 4 Blood Sun, 4 Exploration, 2 Burgeoning, 1 Fastbond,
4 Diabolic Revelation, 4 Grim Tutor, 2 Time Warp, 1 Worldfire, 2 Emrakul the Aeons Torn, 1 Ulamog the
Ceaseless Hunger, 2 Dimir Machinations; lands 4 Lotus Vale, 4 Scorched Ruins, 3 Lotus Field,
4 Golgari Rot Farm, 4 Rakdos Carnarium, 4 Blooming Marsh, 4 Copperline Gorge, 2 Graven Cairns, 4 City of
Brass (33 lands).

The design rests on one new predicate and one new search axis. The predicate: **Blood Sun is on a
battlefield, so lands have only their mana abilities** (§2). The axis: **Diabolic Revelation's X and
the multiset it fetches** (§4). The USER's go-off ladder (§5) is what keeps that axis from blowing up.
Fastbond, the Lotus lands, Grim Tutor, Machinations, Worldfire and the Emrakul loop hang off existing
machinery with small extensions (§3, §6–§9).

---

## 0. USER guidance for this design, verbatim (2026-11-20)

These quotes bind the design. The parent relayed them during the design session.

**The deck's plan (ledger):**
- *"Just so it is clear, the typical way this deck wins is with extra land drops -> extra turns ->
  looping emrakul. Diabolic Revelation can be used to consistently fetch a new emrakul. Prior to that
  you can use it to get land, Exploration or Fastbond another copy of itself, Time Warp etc. and then
  follow up the next turn with much more mana."*
- *"...overall, it tends to go off pretty quickly after Blood Sun has landed. (though usually not on
  the turn of)"*

**Diabolic Revelation and the go-off:**
- **G1** *"With Diabolic Revelation I think we will need a go-off heuristic for larger X values. The
  idea is that we can come up with sets of cards that allow turns to be chained into the win.
  Naturally we'll also want heuristics as well, but we should be able to avoid large x values messing
  us up, since once X is high enough the game is over. Once you have 22 mana and Diabolic Revelation it
  is easy to search and play Emrakul + get another tutor for next turn."*
- **G2** *"But prior to that we can often chain into Time Warp."*
- **G3** (continuing G2) *"(and get additional lands or extra land playing cards to ramp up our mana)"*
- **G4** (worked example) *"An example of a easier chain would be Revelation for Time Warp, Revelation,
  Exploration, and two lotus lands. As long as you have a remaining land-drop you can play the first
  lotus for Exploration and then the second for Time wark. Technically it is even easier if you get
  Fastbond, though you will need the green held if you have no current land drop."*
- **G5** *"And having these in hand makes it even easier, since you don't need to search pieces you
  already have."*
- **G6** (correction) *"To be clear, though, often you want multiple exploration or fastbond to go off
  in turns like these. Having an exploration out if often not enough since you need to spend the land
  drops earlier. You want to get to an extra turn with what you have remaining."*
- **G7** (the typical go-off turn) *"Once you can play 4 lands in a turn you can search for 4 Scorched
  Ruins (or 3 and one lotus), emrakul and Diabolic Revelation, play Emrakul and begin looping. It
  typically goes something like that after you have the extra land drops from the Time Warp extra
  turn."*
- **G8** *"(naturally you can also get exploration or fastbond and play it if you have the mana to do
  so)"*
- **G9** *"(to make it possible to reach those land drops in one turn)"* and *"But if you cannot reach
  that, there is the extra time warp you can get in the same way to build up your lands."*
- **G10** (on the ladder: (1) Emrakul go-off, (2) Time Warp bridge, (3) ordinary search) *"We will
  probably still heuristically prune the regular search case, but having this go-off should help. Note
  that 2 should also be included in the go-off. There is always a bridge to 1 if you can do 2."*
- **G11** (refining G10) *"Well, if you are casting Revelation and putting out extra lands anyway.
  Maybe not if you just have Time Warp in hand."*

**Own death (DECIDED):**
- **D1** *"Yes, running out of life is a loss, so the deck should not fastbond so much that it loses."*

How each quote is applied:
- G5 is applied net of HAND only. G6 supersedes any "net of battlefield" reading: an Exploration or
  Fastbond already in play does not satisfy a package. The resource is the land drops still REMAINING
  this turn.
- G10 and G11 make stage (2) part of the go-off, with a precondition: a Revelation is cast this turn
  and the fetched extra lands are put out. A bare Time Warp from hand stays in ordinary search.
- D1 is implemented in §7.

---

## 1. Survey: what exists today (evidence)

### 1.1 Card data
Twelve of the deck's 20 names are absent from `src/cards/data/cards.json`. Present:

| Card | Entry |
|---|---|
| Exploration | `{"static_extra_land_drops":1}` |
| Burgeoning | `{"opponent_land_play_put_land_from_hand":true}` |
| Rakdos Carnarium | `{"produces":["B","R"],"produces_amount":2,"enters_tapped":true,"etb_bounce_land":true}` |
| City of Brass | `{"produces":[WUBRG],"tap_self_damage":1}` |
| Blooming Marsh | `{"produces":["B","G"],"fastland_max_other_lands":2}` |
| Time Warp | `{"extra_turns_on_resolve":1,"targeting":"player"}` |
| Emrakul, the Aeons Torn | `vanilla_creature`, `{"cant_be_countered":true,"cast_trigger_extra_turns":1,"annihilator":6,"put_into_graveyard_shuffles_graveyard":true}` |
| Ulamog, the Ceaseless Hunger | present (cast-trigger exile two; attack exiles 20) |

No land in `cards.json` produces three or more mana per tap today. The largest is Ancient Tomb, with
`produces_amount` 2.

### 1.2 Land entry: three bodies, no ability-suppression concept
- **`PlayLandCore`** (`src/ai/LandPlay.cpp:22-291`) is the single body for every land DROP. It is shared
  by:
  - the executor: `AIEngine::TryPlaySpecificLand` and `TryPlayLand`;
  - the rollout: `PlayLandByName` (`TurnSolver.cpp:~83037`), which serves `SimulateLandPlay`,
    `ApplyPlanDirect`, `WithPlanLandDrop` and the enumeration probe;
  - the provider probe `PlayLandDrop` (`DecisionProviders.cpp:~46922`).

  It runs these steps in order:
  1. fetch collapse;
  2. Lake of the Dead replacement (`:95-132`);
  3. tapped decision;
  4. rad choice;
  5. depletion / mana counters;
  6. type and colour choice;
  7. energy;
  8. Amulet untap;
  9. Gateway Plaza;
  10. scry / surveil;
  11. Karoo bounce;
  12. lifegain;
  13. Orchard spawn;
  14. landfall.
- **`EnterLand`** (`src/core/SpellEffects.h:~53483`) handles lands PUT onto the battlefield: Burgeoning,
  fetches, `PerformLandSearch`, Genesis Wave, Manabond and others. It re-implements most of those steps.
  The Karoo bounce and lifegain run through `ResolveLandEnterEtbExtras` (`:~53412`).
- **Opponent land entry** (`src/core/OpponentDeck.h:226-240`) has its own tapped logic. Blood Sun
  affects the opponent's lands too.
- **Enters-tapped authority:** `LandWouldEnterTapped` (`SpellEffects.h:~49510`). It covers Gond Gate,
  shocks via the provider hook, reveal, fastland, checkland and `enters_tapped`. Its wrappers are
  `LandUsableOnEntry`, `LandEntryHasChoice`, `ApplyLandUntapPayment` and `LandEntersTapped`. Several
  re-implementations exist:
  - `BounceReplayEntersUntapped` (`DecisionProviders.cpp:~6298`);
  - `PregateEntersTappedAlways` (`:~48877`);
  - `BounceCertainlyClean` / `BouncePossiblyClean` (`TurnSolver.cpp:~95266`);
  - `OpponentDeck.h:234`;
  - about 60 raw provider reads of `params.enters_tapped`. These are heuristics (ranking), not
    correctness.
- **Tap pain:** the single predicate is `dmgev::PainForTap` (`src/core/DamageEvents.h:~209`). The life
  deductions happen in:
  - `TapSourceIntoFloat` (`ManaPayment.cpp:~644-745`);
  - `TapForCostBacktrackWorker` (`SpellEffects.cpp:~3820`, pain at `~4853`);
  - `TapDripLandsIfUseful` (`SpellEffects.h:~2423`);
  - `FloatLandBeforeSacrifice`.

  **`tap_self_damage` CONFLATES two rules objects.** City of Brass's ping is a *triggered* ability
  ("Whenever this land becomes tapped"), so Blood Sun removes it. A painland's or Ancient Tomb's damage
  is *part of the mana ability*, so it stays. No param distinguishes them today.
- **Keys:**
  - `land_sig` / `land_bonus` in `EnumeratePlansWithLandUncachedBody` (`TurnSolver.cpp:~104489-104708`)
    fold hand lands on STATIC params, including `pd` (pain), `kb` (Karoo), `fl` (fastland), `s` / `sv`
    (scry / surveil).
  - **`ManaCacheKey` (`SpellEffects.cpp:~5811`) hashes only mana sources.** A non-source Blood Sun
    would therefore not enter the key, and a cached City-of-Brass pain result could replay stale.
  - `behaviour_identity` (`CardDatabase.cpp:107-142`) is a static hash of the whole JSON entry. It is
    used as the copies/twins identity by `LandSearch.h`.
- **Precedents for ability changes:**
  - "Islandize" (`LandTypeChangedToIsland`, `SpellEffects.h:~2508`) is gated by a deck stamp
    (`GameState::deck_has_islandize`, set in `GoldFishRunner::StampDeckTraits`) and checked at about 39
    sites one by one.
  - Oko's Elk renames the card, which also loses its mana abilities, so it does not fit here.
  - Nothing implements "lose all abilities" for lands.

### 1.3 Tutors, X, multi-pick, transmute
- **Single tutor, `tutor_to_hand`.**
  - The target is SEARCHED: `MTG_TUTOR_AXIS_RESOLVE` is on by default (`EngineFlags.h:~2506`). The fan
    is in `EnumeratePlansWithLand` (`TurnSolver.cpp:~96701-96812`); the pin is `Plan::tutor_choice`; the
    width is `TutorSearchWidth()` (default 6, `DecisionProvider.h:3548`).
  - Two gaps:
    - the width cap is not lifted at unlimited budget;
    - **one tutor per plan**: the second tutor resolves to the provider's front (audit gap A4/C6,
      `docs/design/no-greedy-in-search-window.md`).
  - Executor: `EffectHandler.cpp:~568`. Rollout: `TurnSolver.cpp:~77267`. Body: `PerformTutor`
    (`SpellEffects.cpp:240-597`).
  - **No "lose N life on resolve" param exists** (needed for Grim Tutor).
- **Ranger of Eos (`etb_tutor_hand_count`) is NOT a searched precedent.**
  - `TutorHandPutList` is a provider pick at resolution (`DecisionProvider.h:508-516`), and its
    default is the first N copies in library order. This is the shape that was ruled an in-window
    greedy for `SacTutorPutList`.
  - Revelation must NOT copy it. The searched multi-pick precedents are:
    - **Tooth and Nail**: cast-time subset fan, one cast `Action` per (mode, subset), copies folded
      by sorted name key (`TurnSolver.cpp:~45191-45258`, `ResolveToothAndNail`).
    - **Land search**: multiset enumeration over identity classes; "up to N" collapses to maximal
      picks with a written dominance proof; provider order plus search pin (`src/core/LandSearch.h:35-52`).
    - **Defense of the Heart**: `SacTutorPutOptions`.
- **X.**
  - `CollectActions`' {X} block (`TurnSolver.cpp:~42551-43130`) ends with
    `if (def.tmpl != CardTemplate::DirectDamage) continue;`. **Any other {X} card is dropped from
    enumeration unless it gets its own branch** (the "Luxurious Libation trap", `:~42986`).
  - `GenericProvider::XCandidates` returns only the maximum affordable X
    (`DecisionProviders.cpp:~8973`). Human play gets every X through the `x_value` decision.
- **Transmute.**
  - `transmute_cost` (Dimir House Guard): `CollectActions` emits one `Kind::Channel` per distinct legal
    name, with no width cut (`TurnSolver.cpp:~53244`).
  - `ApplyTransmute` (`SpellEffects.h:~24800`) runs in both worlds. Viewer: the `tutor` decision
    (DECISIONS.md row "Dimir House Guard's Transmute").
- **Shuffles are deterministic.**
  - `ShuffleAfterSearch` → `Library::ShuffleByKey`, keyed per card (`SpellEffects.h:~2899`). This is
    the common-random-numbers design: removing a card leaves the relative order of the others
    unchanged.
  - The search is clairvoyant (it reads the real library order). The non-clairvoyant brackets are
    `MTG_NC_SEARCH` and `MTG_SEARCH_BLIND_UNKNOWN`.
  - `PerformEtbTutorToHandMulti` shuffles ONCE for a multi-fetch. A new multi-tutor must do the same.
- **Look-at-top primitives** (`ScryTop`, `ReorderTopOrShuffle`, `SurveilTop`, `EnumerateTopDispositions`,
  `SpellEffects.h:~50435-50953`) act only on `ActivePlayer().library`. **Nothing implements "look at the
  top N of TARGET PLAYER's library, exile any number, rest in any order".**
  `DeckTouchesOpponentZones` (`GoldFishRunner.cpp:635-659`) is a param scan, and Machinations would
  need a term in it.

### 1.4 Land drops, extra turns, the Emrakul loop
- **Drops.**
  - `Player::LandDropsAvailable() = 1 + bonus_land_drops_this_turn` (`src/core/Player.h:200`). There
    are 86 readers.
  - Untap resets the bonus to `StaticExtraLandDrops` (`GameEngine.cpp:450`; rollout twin
    `TurnSolver.cpp:~82643`). An Exploration that enters mid-turn adds its drop at once
    (`SpellEffects.h:~12124`).
  - Each plan carries one drop. **Site-10 breakpoint**: when a section played a land or opened a drop
    and a drop is still open with a land in hand, the continuation re-enumerates
    (`TurnSolver::LandDropSnapshot` / `LandDropNewlyOpen`, `TurnSolver.cpp:~37384`). Lands arriving in
    hand arm the same site.
  - The beyond-horizon greedy (`SimulateLandPlay`, `AIEngine::TryPlayLand`) caps at `guard < 8`, i.e.
    9 lands per turn.
  - **No unlimited-drops concept and no damage-per-land-played trigger exist.**
- **Extra turns.**
  - `cast_trigger_extra_turns` / `extra_turns_on_resolve` are handled in both worlds
    (`EffectHandler.cpp:1289/813`, `TurnSolver.cpp:~75961/77839`).
  - `AdvanceTurnCounter` does not bump `turn_number` for an extra turn (`GameState.h:~783`), which is the
    USER ruling: win turn = round.
  - `DepthAcrossTurn` (`TurnSolver.cpp:116260`) gives the ply back on entering an extra turn, so extra
    turns in the window are searched at full depth.
  - `deck_has_extra_turn` disables the turn-edge prunes and the winless certificate.
  - `TurnSeqCapped` (4 × max_turns) is the safety stop.
- **Emrakul loop pieces.**
  - `put_into_graveyard_shuffles_graveyard` → `PerformGraveyardShuffle` (`SpellEffects.h:~3312-3375`).
    It is hooked in `OnCreatureDies`, destroy and discard (cleanup included), in both worlds. Exile does
    NOT trigger it.
  - The legend rule: `EnforceLegendRule` (`:~30424`) → graveyard → `OnCreatureDies` inside a
    `GyShuffleBatch`, so **the legend-rule death shuffles**. The keep choice is searched (the
    `CollectActions` LEGEND-KEEP pass, `TurnSolver.cpp:~54120`).
  - **BLOCKER: the second Emrakul is never offered.**
    - `DecisionProvider::OfferDuplicateLegendCast` (`DecisionProviders.cpp:3473`) prunes a duplicate
      legendary `vanilla_creature` cast. It makes an exception only if `DuplicateCastHasUpside`,
      `DuplicateEntryOrDeathHasUpside` or `HandShedIsPayoff` says so.
    - `DuplicateCastHasUpside` (`:2207`) looks only at WATCHER cast triggers
      (`cast_trigger_creates_tokens`), never at the spell's own `cast_trigger_extra_turns`.
    - `DuplicateEntryOrDeathHasUpside` (`:2146`) does not look at `put_into_graveyard_shuffles_graveyard`.
    - So the loop's recast is pruned today (§8).
- **Branch-and-bound on win turn already exists.**
  - The la_cand loop skips a candidate when `pass_best_win <= copy.turn_number`, and otherwise asks it
    only for a strictly earlier win (`TurnSolver.cpp:~129081-129098`).
  - A this-turn win returns immediately as "the earliest possible from here" (`:~120319`).
  - Because extra turns keep `turn_number`, **a win found inside an extra-turn chain at round R is
    exactly "a win this turn" for these cuts.**
- **The verified chain walk exists.** `ExtraTurnChainTrial` (`TurnSolver.cpp:~110211`) is a depth-zero
  continuation through consecutive own turns, accepted only when the simulation reaches a real
  same-round win. It is enabled per provider by `ExtraTurnChainWalkEnabled()`, which is true only for
  `ExtraTurnsProvider` (`DecisionProvider.h:1568`).

### 1.5 Own death, routing, mana
- **Own death.** `own_death_live` (`GameState.h:916`) is set in `GoldFishRunner.cpp:~2050-2079` from:
  - `dmg_events_armed`;
  - `MTG_OWN_DEATH_ALL`;
  - a life-cost card in the main or side (`hellbent_upkeep_life_loss`, `tap_blood_cost`,
    `tap_is_life_payment`, `damage_per_nonbasic_land`, `etb_damage_each_player_per_nonbasic_land`);
  - Stuffy Doll's own `etb_choose_player`. That is the precedent: **arm from the card's OWN param**.

  City of Brass's `tap_self_damage` does NOT arm it. Unarmed decks treat life as bookkeeping.
- **Routing.**
  - Today the deck routes to **`GenericProvider`** (`DetectDecisionProvider`, `DecisionProviders.cpp:~29806`;
    returns at `:~30730-30925`):
    - Time Warp sets `extra_turn_resolve` but not `extra_turn_signature`;
    - Emrakul sets `cy_extra_turn`, but there is no Cyclone;
    - nothing else trips a signature.
  - **Trap:** adding Grim Tutor or Revelation with `tutor_to_hand` sets `anti` (`:~30645`) and moves the
    deck to `AntiLifegainProvider`.
- **Mana.**
  - One-colour bursts are supported by both payers: `IsSingleColorBurstSource`
    (`mana_per_life_gained || produces_one_color`), the `one_colour_burst` branch in `TapSourceIntoFloat`
    (`ManaPayment.cpp:~812-830`), and the backtracker twin (Accomplished Alchemist precedent).
  - A single-colour `produces` with `produces_amount` N gives N of that colour (depletion lands).
  - `ManaPool` fields are ints, with no cap. One soft limit: `PrepayWildTrue` holds 8 slots of up to 8
    units, and any overflow is released as any-colour mana (`GameState.h`).
- **Second main.** `GoldFishRunner::DeckUsesSecondMain` (`GoldFishRunner.cpp:57-~328`): none of the
  deck's cards trips it.

---

## 2. Blood Sun: "All lands lose all abilities except mana abilities"

### 2.1 Rules
- **CR 613.1f (layer 6).** Blood Sun removes every ability of every land on every battlefield except
  mana abilities. A mana ability is one that produces mana (CR 605.1a). An ability that merely COSTS
  mana is not a mana ability (Blood Sun ruling 1).
  - Graven Cairns' `{B/R}, {T}: Add {B}{B}/{B}{R}/{R}{R}` produces mana, so it **stays**.
  - A painland's damage is part of its mana ability, so it **stays**.
  - City of Brass's "Whenever this land becomes tapped, it deals 1 damage to you" is a triggered
    ability, so it is **removed**.
  - Lotus Field's Hexproof is **removed** (inert in goldfish).
- **Entering.** Rulings 3-4 and CR 614.12: a land entering while Blood Sun is on the battlefield has
  already lost its replacement and "as enters" abilities when they would apply, and its "when enters"
  triggers never trigger. So under Blood Sun:
  - Karoos enter untapped and bounce nothing.
  - Lotus Field enters untapped and sacrifices nothing.
  - Lotus Vale and Scorched Ruins enter with no sacrifice.
  - Fastlands are untapped regardless of land count.
  - Shocks are untapped with no life paid. No `land_entry` decision is shown.
  - Scry, surveil and lifegain lands do nothing.
- **Land cards not on the battlefield are unaffected** (ruling 2). Cycling and other hand abilities
  stay; the deck has none.
- **Timing.**
  - Blood Sun resolving mid-turn removes abilities from then on. A land that already entered tapped
    stays tapped, and an already-resolved bounce stays resolved; nothing is retroactive.
  - Blood Sun is a sorcery-speed enchantment, so it can never resolve while a land's enter trigger is
    on the stack: the engine never needs the "trigger already on the stack" case.
  - Blood Sun leaving: in this list only Worldfire removes it, and Worldfire exiles every land
    simultaneously. A land entering AFTER the last Blood Sun leaves has its abilities again.
  - Two Blood Suns are redundant.
- **Granted abilities.** Ruling 5: an ability a land GAINS after Blood Sun entered (later timestamp)
  is kept. Ruling 6: Blood Sun stops a land from granting abilities to others, but type-changing land
  abilities still apply (layer 4 before layer 6). Neither occurs in this list; both are self-guard
  items (§2.4).
- **Blood Sun's ETB "draw a card"** is a real draw: a new card in hand mid-turn, so it arms the
  draw/put-in-hand breakpoint. It is not a land ability.
- Exploration, Burgeoning and Fastbond are enchantments, so they are unaffected.

### 2.2 The predicate
```
// src/core/SpellEffects.h (next to LandWouldEnterTapped)
inline bool LandNonManaAbilitiesSuppressed(const GameState& s)
{
    if (!s.deck_has_land_ability_loss) { return false; }       // every other deck: one bool, identity
    for (const Permanent& p : s.battlefield)                     // BOTH controllers: "All lands"
        if (const CardDefinition* d = LookupCached(p.card); d && d->params.lands_lose_nonmana_abilities)
            return true;
    return false;
}
```
- `CardParams::lands_lose_nonmana_abilities` (bool) is Blood Sun's param.
- `GameState::deck_has_land_ability_loss` is stamped in `GoldFishRunner::StampDeckTraits`, from the
  mainboard and sideboard. This is the islandize precedent. With the stamp false, every other deck is
  byte-identical.
- The battlefield is scanned rather than counted. The scan runs only under the stamp, the battlefield
  is small, and a counter would need every leave path kept correct. If profiling ever shows the scan,
  replace it with a counter maintained at the enter and leave chokepoints, verified under
  `MTG_BLOODSUN_VERIFY`.

### 2.3 Where it is consulted: generic gating (preferred, chosen)
Cards are generic, so the predicate gates **the whole land-entry family and the tap-trigger family for
every land**, at these chokepoints:

| # | Site | Under suppression |
|---|---|---|
| G1 | `LandWouldEnterTapped` and every wrapper (`LandUsableOnEntry`, `LandEntryHasChoice` → false, `ApplyLandUntapPayment` → no-op, `LandEntersTapped`). Also `GateEntersUntappedByGond`, which is Gond Gate's own ability and so is lost too, though nothing here is tapped anyway. | enters untapped, no choice offered |
| G2 | `PlayLandCore` steps 2, 4-7, 9-13: replacements (Lake, and Vale/Ruins from §3), rad, depletion / mana counters, colour and type choice, energy, Plaza, scry / surveil, Karoo bounce, lifegain, Orchard. **NOT** step 8 (Amulet is an artifact) and **NOT** step 14 (landfall abilities of OTHER permanents). A land's OWN landfall ability is a land ability and is suppressed; none exist in this list. | skipped |
| G3 | `EnterLand` + `ResolveLandEnterEtbExtras`, and the second-pass callers (Genesis Wave, `PerformLandSearch`, Manabond, Burgeoning, put-from-hand) | same as G2 |
| G4 | `OpponentDeck.h:226-240` | untapped |
| G5 | `dmgev::PainForTap`, and every pain-pricing reader that does not go through it (`PaymentDamageLowerBound`, `PaymentPainSafe`, `BtPainlessMode`, `TapDripLandsIfUseful`). It gets a new param, `tap_self_damage_is_trigger` (bool, City of Brass only). | trigger-pain is 0; mana-ability pain unchanged |
| G6 | `IsForbiddenOrchard` sites (the turn-start spawn and its rollout twin; `LandPlay.cpp:267`) | no spawn |
| G7 | `ManaCacheKey`: add one bit, `LandNonManaAbilitiesSuppressed(s)`, when the stamp is live | prevents stale City pain |
| G8 | `land_sig` / `land_bonus`: compute the non-mana components (`pd` only if a trigger, `kb`, `fl`, `ck`, `s`, `sv`, …) as zero under suppression | more folding, exact |
| G9 | `EffectiveLandIdentity(s, def)`: `behaviour_identity` of the **mana-only projection** of the params when suppressed, else `behaviour_identity` | Lotus Vale ≡ Lotus Field (§4.3) |

- **Mana-only projection.** At load, each land `CardDefinition` gets a `mana_only_identity` hash, built
  from its params with every NON-mana field reset to default. The non-mana field list is the §2.4
  inventory, so one list drives G8, G9 and the self-guard.
- **Provider heuristics** (the ~60 raw `enters_tapped` reads) only order and rank, so they need no
  gating for correctness. `BloodSunProvider` overrides the ones it uses (discard and keep rank) to read
  the effective value. Other providers are untouched.
- **Viewer emitters.** `WriteLandEntryDecisionJson` is never reached, because `LandEntryHasChoice` is
  false. The `bounce` and `sacrifice` choosers are never called, because their call sites sit inside
  G2/G3.

### 2.4 The rest of the land-ability inventory: a named caveat with a loud self-guard
These families have NO generic gate in this pass. They are activated non-mana abilities and statics
scattered across about 8 enumerator tables (`BpAvailablePermAbilityModes`, `CollectActivationKeys`,
the `CollectActions` ModeSpec table, `NumberedTapSource`, `CardHasPostEntryActivation`,
`BpActivationAbilityUnambiguous`, `BoardActivationIllegalReason`, `CheckLineExact`), plus bespoke code.
None is in this deck.

- **Activated (non-mana):**
  - `fetch_land_types`;
  - `land_search_activation_*` / `land_search_*`;
  - `sacrifice_draw_cost`;
  - `can_animate` / `animate_*`;
  - `gy_return_*`;
  - `tap_target_pump_*`;
  - `tap_token_*`;
  - `tap_tutor_put_*` and `alt_win_distinct_named_*` (Maze's End);
  - `tap_treasure_*`, `tap_damage_*`, `tap_blood_*`, `team_pump_*`, `untap_creature_*`,
    `threshold_loot_cost`, `self_bounce_cost`, `sac_lifegain_per_creature*`, `tap_draw_*`,
    `tap_investigate_cost`, `storage_charge_mode` (Mercadian Bazaar's counter mode).
- **Triggers and statics on the battlefield:**
  - `upkeep_gy_reanimate*` (Emeria);
  - `storage_land` upkeep (Dwarven Hold);
  - `own_creature_enters_lifegain` (Seraph Sanctuary);
  - `no_max_hand_size` (Reliquary Tower);
  - `all_nonbasic_land_types` (Planar Nexus: a type change, layer 4, so it would actually STAY);
  - `tap_is_life_payment` when it is a static (Thran Portal);
  - `etb_choose_color` lands whose mana ability names the chosen colour: under Blood Sun no colour is
    chosen, so that mode makes nothing;
  - `colored_creature_only` (Cavern / Unclaimed Territory).
- **Granting or receiving:** `land_aura_sets_island` and other land-granted abilities (ruling 5/6).

**Self-guard.** In `StampDeckTraits`, when `deck_has_land_ability_loss` is set, scan our mainboard and
sideboard and the selected opponent list. If any land carries a field from this list, abort at setup
with `[bloodsun] <card> has <param>, an ungated land ability under Blood Sun; gate it before playing
this list`. A unit test pins the list against `CardParams`: adding a land param without classifying it
as gated, mana, or self-guarded fails the test. That makes the inventory a living contract, not a doc
table.

---

## 3. The Lotus lands and the Tier-1 lands

### 3.1 Lotus Vale, Scorched Ruins (replacement: sacrifice two UNTAPPED lands or go to the graveyard)
**Params:**
- `etb_replace_sacrifice_untapped_lands: 2` (int; new);
- Vale: `produces: [W,U,B,R,G]`, `produces_amount: 3`, `produces_one_color: true`;
- Ruins: `produces: ["C"]`, `produces_amount: 4`.

**Where it runs.** The replacement goes next to the Lake replacement in `PlayLandCore` (step 2), and in
`EnterLand` for lands that are put (Burgeoning). Both are gated by G2/G3: **under Blood Sun the
replacement does not exist.**

**Without Blood Sun:**
- **The victims must be UNTAPPED.** So, unlike Lake of the Dead, `FloatLandBeforeSacrifice` must NOT
  run: tapping a victim for mana first makes it an illegal victim.
- Vale itself is not on the battlefield yet, so it cannot be a victim.
- **With fewer than two untapped lands** the card goes to the graveyard, never enters, and the drop is
  spent (CR 305.1: playing the land is the special action; the Lake precedent increments
  `lands_played_this_turn`). Fastbond would still count it as played; see §11 R1.
- **Which two** is a searched multiset over untapped-land classes, folded by `EffectiveLandIdentity`.
  It is a pin axis, surfaced to the human as two `sacrifice` clicks (Lake precedent). The menu is at most
  C(k+1, 2) over k classes, which is small.

**Fold V1, sound dominance:** never offer a Vale/Ruins drop that cannot sacrifice. This is the
`LakeDropWithoutVictim` precedent. Proof: compare "play it, it goes to the graveyard" with "hold it".
Holding:
- leaves the same library and the same battlefield;
- leaves the card in hand instead of the graveyard (and a cleanup discard can still put it there);
- keeps the land drop;
- avoids a Fastbond ping;
- keeps `lands_played_this_turn` lower, so later plays ping no more.

The only thing the graveyard copy could buy is an Emrakul graveyard shuffle returning it to the library,
and a card in hand weakly dominates the same card in the library. Condition: no permanent we control
benefits from a land being PLAYED (a "whenever you play a land" upside watcher). Today no card has one,
and Burgeoning watches opponents only. The fold checks this generically.

**Is playing a Vale that CAN sacrifice useful?** Yes, and it stays searched. It nets +1 mana this turn
(3 − 2 untapped lands) and is a 3-mana land from then on. Under Blood Sun the question disappears.

### 3.2 Lotus Field (enters tapped; ETB trigger: sacrifice two lands)
**Params:**
- `enters_tapped: true`;
- `etb_sacrifice_lands: 2` (int; new; a TRIGGER);
- `produces` WUBRG ×3 with `produces_one_color`;
- keyword `Hexproof` (inert).

**Resolution.** This is a trigger, so it uses `ResolveLandEnterEtbExtras` (second pass) in both bodies,
gated by G2/G3. Under Blood Sun Field is untapped and sacrifices nothing.

**Without Blood Sun:**
- Tapped victims are legal. "Tap two lands for mana, then play Field and sacrifice them" is the real
  line: you hold priority before the land play, and mana empties only at the end of the step. So
  `FloatLandBeforeSacrifice` applies (Lake precedent).
- **With fewer than two OTHER lands, sacrifice every land, Field included** (ruling).

**Folds:**
- **F-L1, sound.** Prefer a TAPPED victim over an untapped one of the same class. The futures are
  identical, except that the kept untapped copy can still produce mana this turn.
- **F-L2, sound, the V1 shape.** Never offer a Field drop with fewer than two other lands. Holding it
  dominates: those lands survive, the drop is kept, and the card stays in hand.

**Which two** is a searched multiset over land classes, with tapped-ness as part of the class after
F-L1.

### 3.3 Golgari Rot Farm, Copperline Gorge, Graven Cairns: Tier 1, existing params exactly
- **Golgari Rot Farm:** `{"produces":["B","G"],"produces_amount":2,"enters_tapped":true,"etb_bounce_land":true}`.
  This is the Rakdos Carnarium shape. The "you control no other lands → return itself" ruling is
  already handled: `karoo_self_bounce` is never enumerated, and `BounceKarooLand` bounces itself when it
  is alone.
- **Copperline Gorge:** `{"produces":["R","G"],"fastland_max_other_lands":2}`. This is Blooming Marsh's
  shape; its simultaneous-entry ruling is the existing fastland semantics.
- **Graven Cairns:** `{"produces":["B","R"],"is_filter":true}`. This is Fire-Lit Thicket's shape. The
  `{T}: Add {C}` first ability is the implicit free {C} mode of every `is_filter` land
  (`ManaPayment.cpp:~9202`, `:~13543`). The fed mode is simulated as mana-neutral colour fixing
  (`SpellEffects.h:~56244`). Both are mana abilities, so Blood Sun does not touch them.

### 3.4 Re-review of the present lands under Blood Sun (outcomes the tests pin)
| Land | No Blood Sun (unchanged) | Under Blood Sun |
|---|---|---|
| Rakdos Carnarium / Golgari Rot Farm | tapped, bounce a land | untapped, no bounce, {B}{R} / {B}{G} |
| Blooming Marsh / Copperline Gorge | untapped iff ≤ 2 other lands | always untapped |
| City of Brass | 1 damage per tap (trigger) | no damage |
| Graven Cairns | {C} or filter | same |
| Lotus Vale / Scorched Ruins | sacrifice 2 untapped, or graveyard | enters untapped, free |
| Lotus Field | tapped + sacrifice 2 | untapped, free |

---

## 4. Diabolic Revelation: the X and the multiset

**Params:** `tutor_up_to_x: true` (new), `tutor_shuffle_after: true`; `mana_cost "{X}{3}{B}{B}"`.
Its cost and colour demand: {B}{B}, generic 3 + X.

### 4.1 Shape: a cast-time PACKAGE axis (Tooth and Nail shape), not nested picks, not the Ranger shape
Each Revelation cast `Action` carries its whole choice: `chosen_x = |S|` and the package `S` as class
counts. The package is stored in a per-node package table indexed from the Action, or as a compact
vector on the Action, whichever `Action`'s size discipline allows.

**Resolution.** `PerformTutorPackageToHand(state, S)` moves the named classes from the library to hand,
then shuffles ONCE (the `PerformEtbTutorToHandMulti` precedent). It is called from:
- `EffectHandler.cpp` (executor);
- `apply_one` in `ApplyPlanDirect` (rollout).

Both read the same Action field, so the two worlds stay in lockstep.

**Why this shape:**
1. **The tutor-pin gap.** The existing plan-level `tutor_choice` axis varies one tutor per plan
   (gap A4/C6). A plan holding two Revelations, or Revelation plus Grim Tutor, would hand the second one
   to the provider's front, which is an in-window greedy. Cast-time encoding gives every cast its own
   searched choice.
2. **No silent drop.** It is the "own branch" the {X} block requires, so the card is not silently
   dropped.
3. **Ordered generation, short-circuited by the existing B&B (§4.4).** Generation needs whole packages
   anyway: the recognizer (§5) reasons about sets, and a nested pick-by-pick decision has no evaluation
   at inner nodes, so it buys no pruning. It would cost the same number of leaves with a deeper tree.
4. **Budget pricing.** A whole package is the natural unit for a per-link price (§4.4).

The library at cast time equals the library at resolution, because nothing in this pool acts in
between. A Blood Sun ETB draw comes before or after a cast, never inside one. Tooth and Nail relies on
the same identity.

### 4.2 X
- `X = |S|`, and X ≥ |S| is forced. **Fold X1, sound:** paying X > |S| is dominated by X = |S|. The
  only difference is spent mana. Under Blood Sun spending mana has no upside, and without Blood Sun
  extra taps are worse (City pings).
- **Fold X0, a real-information identity:** X = 0 is not offered. A 5-mana shuffle changes only the
  post-shuffle library order, so it is useful only through clairvoyance. This is the same standard
  `LandSearch.h` uses to fold copies. It is disclosed in 6a.
- `1 ≤ X ≤ min(X_max, |library|)`. `X_max` is computed from the turn pool, widened by same-plan
  producers, which here are lands only. The {X} block's existing producer widening handles that.
- `XCandidates` is NOT used for this card: the package axis subsumes X.

### 4.3 The multiset space and its sound folds
**Classes:** library cards grouped by `EffectiveLandIdentity` for lands, and by name for nonlands.

**Folds:**
- **F-R1, copies.** Which physical copy is taken is visible only through the post-shuffle order
  (clairvoyance), as in `LandSearch.h`.
- **F-R2, twins under Blood Sun.** Lotus Vale ≡ Lotus Field, by G9. Proof: under Blood Sun both are
  "untapped, {T}: three of any one colour". Hexproof is removed too, and is inert anyway. A copy fetched
  to HAND stays a twin until it is played, because in this pool only Worldfire can remove Blood Sun,
  and Worldfire exiles the hand as well. The condition is "a Blood Sun is on the battlefield now, and no
  Blood Sun removal other than an exile-all-hands effect exists in either decklist". That is checked
  generically from params.
- **F-R3:** X = |S| (X1).
- **F-R4:** X ≥ 1 (X0).
- **F-R5, dead classes, sound only where provable:** none in this list. Every class can matter this
  turn or next. Worldfire is the closest (§9.3) and is handled as an ORDER (last) plus the USER question
  U2, not as a fold.

**Size.** There are about 19 classes under Blood Sun, with per-class counts. The space is about
C(19+X−1, X) minus the count limits: ~3·10⁴ at X = 5, ~10⁶ at X = 8. **This is the hot spot, and it is
contained by §4.4 and §5, not by folds.**

### 4.4 Generation, waves, and the B&B short-circuit (USER G1: "avoid large x values messing us up")
**`RevelationPackageGen`** is a LAZY generator. It yields packages in provider order:
1. the go-off packages (§5: stage 1, then stage 2);
2. the ordinary packages, best-first by the provider's order key (§5.4);
3. then every remaining multiset, in a fixed canonical order (class index, X ascending).

The generator is complete: draining it yields every multiset exactly once (dedup by canonical class
vector).

**Waves.** The `CollectActions` emission admits a prefix of the generator per budget WAVE:
- wave 0 = every go-off package, plus the first K0 ordinary ones;
- wave k admits the next K0·2^k.

Admission is priced per package through `TurnSolver::ChargeDecisionUnits`, which returns immediately when
`budget == nullptr`. So the price is **inert at unlimited budget and monotone below it.** This is the
`MTG_EQUIP_ROUTE_NODE_UNITS` shape the completeness rule names as GOOD. Nothing is refused at any
budget, and raising the budget admits proportionally more packages.

**The B&B short-circuit, (b) in the parent's brief, PROVED:**
- **Claim.** Let the node be at round R, with `state.turn_number == R`. If any admitted package leads
  (through the searched continuation, extra turns included) to a win with win turn R, then no other
  package can improve the root's primary objective.
- **Proof.** Win turn is the round, extra turns do not advance it (`AdvanceTurnCounter`), and no line
  from a state at round R can win before round R. So R is the minimum.
- **Mechanism.** The existing loop then asks every later candidate only for a STRICTLY earlier win
  (`cand_cutoff = pass_best_win − 1`, `TurnSolver.cpp:~129081`), and skips it outright when
  `pass_best_win <= copy.turn_number`. The this-turn-win return (`:~120319`) ends the node.
- **The generator must stop yielding at that point.** This is the step's main correctness item: the lazy
  generator is never asked for the next package once `pass_best_win == state.turn_number`. So **the
  large-X tail is never materialised.** That is the USER's "once X is high enough the game is over",
  made exact.
- **Tie-breaks.** The la_cand tie-break chain (inf-life proof, graded leaf, `plan.value`) may still pick
  among EQUAL-turn wins. Stopping the generator can change WHICH round-R win is played, never its turn.
  This is the identity the existing this-turn-win return already relies on. The objective is
  `avg win turn`.

**Below the go-off threshold (no win at R):**
- The same loop caps every later sibling at "strictly earlier than the best so far". Once a package wins
  at R+1, each remaining sibling only needs to be checked for a win at R.
- The round-aware one-sided bound **`ProvenNoWinThisRound`** (§5.5, step 9) then discards such
  siblings without a rollout. That is a proof, so it is sound.
- With that bound, the unbudgeted tail costs one cheap bound evaluation per package, not one search per
  package.
- **The residual risk** is a node where nothing wins by R+1 and the bound is weak. There, the drain at
  unlimited budget is genuinely large. It is what the unbudgeted backstop rows will expose, and why the
  case-3 prune (§5.4) exists. See U4.

### 4.5 Human play
Unpruned plan variants would be millions of `main_phase` options. So human play gets:
- one Revelation cast per X, through `x_value`;
- then the multi-pick at resolution, through **`dragon`**: the generic multi-pick, `g_play_dragon_chooser`
  (the Bilbo / Genesis Wave precedent), titled "Diabolic Revelation: up to X cards to HAND". It allows
  copies and allows stopping early ("up to").

The provider's front package is the PRESELECTED default (the Bilbo shape: `ask_human_despite_pin`).
This is bucket A: an existing type at a new call site, `PerformTutorPackageToHand`, which both worlds
share. Ranger's `sac_tutor` ("(put in HAND)") is the closest wording precedent. `dragon` is chosen
because it already supports "any number" with copies.

---

## 5. The go-off recognizer (USER G1–G11)

### 5.1 What it is, and what it is not
`BloodSunProvider::GoOffPackages(state)` is **provider ORDER only**:
- it puts stage-1 and stage-2 packages at the FRONT of the Revelation generator (§4.4);
- it also supplies entry 0 of that menu, which the depth-0 runner, the horizon playout and the human's
  preselection take.

It never removes a package and never decides a node. **A recognised package wins only when the engine
actually plays it to a win at round R.** The witness is the real search line, or the verified d0 chain
walk (§5.6). The existing B&B then cuts the rest (§4.4).

So the recognizer cannot make play lossy. If it misjudges a package, the wrong package costs one
search, and the search continues down the rest of the generator. This is the "searched-with-heuristics"
contract (CLAUDE.md, the completeness rule): order and restrict, never pick.

### 5.2 The resource model (this turn only), net of HAND, on REMAINING drops (G5, G6)
Inputs, all read from the real state:
- **Mana.** Untapped sources, each with an amount and colour set, `produces_one_color` bursts and
  filters, plus the floating pool.
- **Drops.** `drops = LandDropsAvailable() − lands_played_this_turn` (G6: drops already spent earlier in
  the turn are gone). Plus `fastbond_live` (§6).
- **Purchasable drops.** Each Exploration fetched or held is +1 drop for {G}. Explorations stack
  (CR 305.2), so several may be required (G6). A Fastbond fetched or held makes drops unlimited for
  {G} (G4, G8).
- **Cards.** Hand counts by class (G5: pieces in hand are not fetched) and library counts by class.
- **Life.** Life, `own_death_live` (decided, §7), the Fastbond count, Grim Tutor's 3, and City pings if
  Blood Sun is absent.
- **Colour demands:** Revelation {B}{B}; Time Warp {U}{U}; Exploration / Fastbond {G}; Emrakul 15
  generic.
  - **Blue comes only from City of Brass, Lotus Vale and Lotus Field.** The list has no blue dual.
  - Scorched Ruins makes only {C}. So colour decides the set while Time Warp is still needed, and stops
    mattering at the Emrakul stage (G7).

**The sequence it simulates** on an abstract pool:
1. Cast Revelation (X = number of missing pieces + fill).
2. Then, repeatedly: play lands while drops remain, preferring the colours still owed; cast drop-makers
   with {G} (from a held source, or a Lotus played earlier in the sequence; G4's "green held if you have
   no current land drop"); play more lands.
3. Finally cast the stage's finisher.

The order key is "colour-feasible, then the largest end-of-turn untapped-land mana M1 for next turn,
then the smallest X". Heuristic choices inside the recognizer (which land first, which colour a Lotus
makes) are fine, because the recognizer only ORDERS. The engine re-searches every real choice when it
plays the package.

### 5.3 The stages
**Stage 1: Emrakul-loop entry (G1, G7, G8).**

Package: {Emrakul if not in hand} ∪ {Revelation if no OTHER Revelation stays in hand} ∪ land fill ∪
drop-makers. It must satisfy:
- (a) this turn can cast Revelation, the drop-makers and Emrakul ({15}) in the simulated sequence;
- (b) life stays > 0 (with own death live);
- (c) the next turn's untapped-land mana **M1 ≥ 21**.

M1 = every land on the battlefield at end of turn. It is the floor, before E1's own drops and draw.

The canonical G7 package is {Scorched Ruins ×4 (or ×3 + a Lotus), Emrakul, Revelation} with ≥ 4
remaining or purchasable drops: X = 6 (11 mana), then four lands (16), then Emrakul (15).

**Claim L (PROOF of a win this round).** Stage-1 entry wins at round R if:
- (L1) Emrakul#1 resolves this turn;
- (L2) E1 has M1 ≥ 21 with {B}{B}, a Revelation in hand (fetched, or held), and an Emrakul in the
  library (or hand);
- (L3) the opponent's life is ≤ 30 (it is 20, and never rises in goldfish);
- (L4) our life covers the E1/E2 pings (Fastbond and City).

Why:
1. Emrakul's cast trigger gives E1. The turn counter does not move.
2. In E1, Emrakul#1 has been under our control since E1 began, so it attacks for 15 (flying; the
   goldfish opponent never blocks).
3. E1 also casts Revelation (X = 1: Emrakul#2; 6 mana) and Emrakul#2 (15): 21 ≤ M1. Its cast trigger
   gives E2.
4. The legend rule is a state-based action. The SEARCHED keep is the old copy (and F-E1 in §8 makes that
   the only offer). Emrakul#2 dies, which triggers its graveyard shuffle and returns the spent
   Revelations and Time Warps.
5. In E2, Emrakul#1 attacks again: 30 ≥ 20.

With M1 ≥ 22, Revelation at X = 2 also fetches the next Revelation each turn, so the loop runs forever
(it beats any finite life). This is exactly the USER's "22 mana and Diabolic Revelation" (G1). Every
step is existing, cited machinery, except the legend-cast prune (§8, step 7).

**Stage 2: Revelation-driven Time Warp bridge (G2, G3, G4, G9, G10, G11).**

Package: {Time Warp if not in hand} ∪ {Revelation for next turn, if no other in hand} ∪ ramp fill
(the big lands Vale/Ruins/Field, Karoos; drop-makers Exploration ×k / Fastbond / Burgeoning) such that:
- this turn casts Revelation, PUTS OUT the fetched lands (G11: "if you are casting Revelation and
  putting out extra lands anyway"), casts the drop-makers it uses, and casts Time Warp;
- life stays > 0.

Rank by M1 and by next turn's drops. The spare picks (X > 2) are ordered by the G3 ramp fill.

**The canonical G4 package** is {Time Warp, Revelation, Exploration, Lotus, Lotus}, from M0 ≥ 10
(including {B}{B}) with one open drop:
1. Revelation, X = 5 (10 mana).
2. Lotus#1, tapped for GGG.
3. Exploration (G).
4. Lotus#2, using Exploration's drop, tapped for UUU.
5. Time Warp: UUU + GG.

M1 = M0 + 6, with 2 drops next turn. **E1 then reaches stage 1:** Revelation X = 4 {Emrakul, Revelation,
Ruins, Ruins} costs 9, the two Ruins add 8, and Emrakul costs 15. That needs M1 ≥ 16, and M1 = M0 + 6 ≥ 16
holds. M2 ≥ 24 ≥ 22 sustains the loop. **So from 10 mana, Blood Sun out, Revelation in hand and one open
drop, the game is won this round**, given the library holds the pieces (one Time Warp, two Revelations,
one Exploration, two Lotus, two Ruins, two Emrakuls). This is scenario test T1.

The Fastbond variant (T1b): with no open drop, Fastbond's {G} must come from a source held after
Revelation (G4). Both Lotus plays are then non-first plays: 2 damage.

**Is "(2) always bridges to (1)" (G10/G11) a proof? Not unconditionally.** Mana is non-decreasing
across extra turns under Blood Sun: lands only accumulate, and each extra turn untaps everything and
grants fresh drops. But each bridge consumes a Time Warp and a Revelation, and they return only through
an Emrakul graveyard shuffle. The named residual gaps:
1. **Time Warps.** There are 2. If both are in the graveyard or exile and M1 < 21, a third bridge
   cannot happen before Emrakul.
2. **Revelations.** There are 4. Each bridge spends one, and stage 1 needs one more.
3. **Growth.** A minimal bridge (X = 2, no fill, no drops) adds only the mana of lands already in hand.
   It can stall below 21 within the available Time Warps.
4. **Big lands.** There are 11 Lotus/Ruins plus 8 Karoos. Lands already drawn or played may leave the
   library short.
5. **Colour.** Time Warp's {U}{U} needs a City or a Lotus at that moment.
6. **No Blood Sun.** Without Blood Sun, Vale/Ruins/Field cost lands (§3), and mana can fall.
7. **Life.** Fastbond pings, with own death live (D1).

**How the go-off treats it (USER ruling G10):**
- Stage 2 IS part of the go-off. Its packages are ordered at the front, right after stage 1.
- The recognizer runs a two-turn LOOKAHEAD on the abstract model (this turn's bridge, then "does E1
  reach stage 1, or a second bridge with the remaining Time Warp?"). It ranks packages whose lookahead
  closes ahead of those whose lookahead does not. It never drops either kind.
- A bridge counts as a "proof" only when the engine's witness wins at round R. Otherwise it is an order.
  The residual gap is reported to the USER (U5).
- **A bare Time Warp from hand** (no Revelation cast this turn, no lands put out) is NOT stage 2 (G11).
  It stays in ordinary search, with ordinary order.

### 5.4 Stage 3: ordinary search, and the USER's case-3 prune (G10)
Below the go-off, the generator yields ordinary packages ramp-first. The **order key**:
1. lands that raise M1 the most for their colour role;
2. drop-makers whenever drops would be the binding resource;
3. Revelation (a copy for next turn);
4. Time Warp;
5. Blood Sun, if none is in play;
6. Emrakul;
7. Grim Tutor / Machinations;
8. Ulamog;
9. Worldfire last (§9.3).

The provider's `TutorSearchWidth()` override returns "all" for Grim Tutor, so it is complete (§9.1).

**The case-3 prune.** The USER: *"we will probably still heuristically prune the regular search
case"*. It is designed as a **RESTRICT that runs as a final WAVE, not as a refusal.** Packages the
heuristic marks unpromising (for example, any package containing Worldfire, Ulamog, a second Blood Sun
while one is out, or more than N non-land non-engine cards) are moved to the generator's LAST wave.
Under a finite budget they are effectively pruned. At unlimited budget they are still drained (or
B&B-cut). That satisfies both the USER's prune intent and the completeness-at-large-budget rule.

It is adopted per the USER's prune bar (better overall, no unrecognisable lost lines, faster). It ships
default OFF until measured, and the default is the USER's call (U4). If the unbudgeted backstop rows
show that the final wave is still intractable, a HARD restriction is the USER's decision, never an
agent's (U4).

### 5.5 `ProvenNoWinThisRound` (one-sided, round-aware, step 9)
`deck_has_extra_turn` turns off the existing turn-edge certificate, because that certificate is
turn-scoped. This deck needs a ROUND-scoped one. It returns true only when an OPTIMISTIC bound proves
that no win can land this round. A win at round R needs one of:
- (a) an attacker that can attack this turn, with power ≥ the opponent's life;
- (b) an extra-turn source castable this turn under the optimistic mana bound;
- (c) the deck-out / Worldfire routes, bounded the same way.

For (a), Emrakul gives 15, Ulamog 10, and a combination counts.

The optimistic mana bound U = untapped + floating + (open drops + purchasable drops) × 4 (the largest
per-land yield). It is ∞ if a Fastbond is castable or held and life allows.

It is used ONLY as the strictly-earlier test once a sibling already wins at R+1 (§4.4), and as the
provider's `Certificate()`. It ships `NotAssessed` first, and is added as its own measured step: it is a
proof, so labels are byte-identical, and it only buys speed.

### 5.6 The depth-0 chain walk
`BloodSunProvider::ExtraTurnChainWalkEnabled() = true`. The existing verified trial
(`ExtraTurnChainTrial`) plays consecutive own turns at depth 0 and is accepted only on a real same-round
win, with entry 0 of every menu as the go-off front. It is the cheap witness for a long loop. A failed
trial falls back to the full search, so it is the D0Runner/HorizonLeaf permit's legitimate use and never
a substitute. Its five-mana entry pre-check reads Time Warp's cost and already fits.

It is enabled in step 8 and measured there. The same step measures the Extra Turns deck's
priced-decision override, `DecisionPricedLimitFor`: it keys on this hook, so enabling the hook also
pulls in that tighter limit. Decide it explicitly (U8).

---

## 6. Fastbond

**Params:**
- `unlimited_land_plays: true` (a static; new);
- `land_play_damage_after_first: 1` (a trigger; new).

**Representation.** `StaticExtraLandDrops` adds `kUnlimitedDrops = 64` for each live
`unlimited_land_plays` permanent, at untap and on entry (Exploration's mid-turn path). That is exact,
not a cap: no decklist holds 64 lands, and this one holds 33. All 86 `LandDropsAvailable()` readers work
unchanged.

**Leaving:**
- When a Fastbond or Exploration leaves mid-turn (here only through Worldfire's exile-all), the bonus is
  recomputed. Fastbond's permission ends when it leaves, and nothing retroactive happens.
- The new Worldfire step resets `bonus_land_drops_this_turn` to `StaticExtraLandDrops(s)` (now 0) plus
  any one-time grants already used.
- The readers that LOOP up to the drop count are audited, and they loop over lands in hand instead. That
  includes the greedy playout's `guard < 8`, which caps a Fastbond turn at 9 lands: it becomes
  `guard <` lands in hand.

**Damage.**
- **Where it fires.** In `PlayLandCore` only, which is the land-PLAY body. It fires after
  `++lands_played_this_turn`, and only if the new count is ≥ 2. It does not fire in `EnterLand` (lands
  put onto the battlefield; Burgeoning; ruling 2). It fires once per Fastbond.
- **Source and timing.** The damage comes from an enchantment source, through `dmgev` (the
  `DamageEvents.h` pipeline). It applies before any further action, because the trigger resolves while
  we hold priority.
- **Life.** With own death live it can kill us (D1).
- **Which permission paid.** Ruling 1: every non-first play is damaged, including drops granted by
  Exploration.

**Search: land-batch normal form (fold LB, sound, Blood Sun only).**

Conditions:
- (i) a Blood Sun is on the battlefield;
- (ii) the stamp shows no landfall, "land enters" or "play a land" upside watcher in either decklist,
  except damage-only watchers (Fastbond).

**Theorem.** Under (i)–(ii), playing a land that the line will play this turn anyway EARLIER within the
turn never loses.

Proof:
- Under Blood Sun the land only adds an untapped mana ability.
- An earlier play only adds options: more mana, sooner, so X and casts form supersets.
- Nothing else observes a land play except Fastbond's damage. Its total equals (non-first plays) ×
  Fastbonds, whatever the order.
- There is no lifegain in the pool, so life is monotone non-increasing within the turn. Two orders with
  the same total therefore reach the same state-based-action outcome at the end, and the earlier-damage
  order never kills where the other survives: every intermediate total is ≥ the final total.
- The library is untouched.

So each section canonicalises: play this section's chosen lands first, in class order, then the casts.
Site-10 breakpoints remain, for drops opened mid-section and lands arriving in hand.

**Which lands to play stays SEARCHED** when drops are finite: a later-fetched Ruins may want the drop.
**With Fastbond** there is no drop competition, so the choice reduces to the COUNT axis, n+1 options
ordered max-first. Life (D1) is the only reason to hold a land.

**Fold LB2, sound partial-order dominance.** When holding some lands, hold only a dominated class. Under
Blood Sun, source A dominates source B if A can produce every mana B can, in at least the same amount:
Lotus ⊒ any one-mana land; a Karoo with {B}{G} ⊒ Marsh's {B} or {G}. Incomparable classes stay
separate. Ruins (CCCC) vs Marsh (B/G) is incomparable.

**Without Blood Sun** the existing per-drop site-10 machinery runs unchanged, because Karoo bounce,
enters-tapped and the sacrifices make order matter.

---

## 7. Own death (DECIDED, D1)

`own_death_live` is armed from the new cards' OWN params, which is the Stuffy Doll precedent. A term is
added to `life_cost_live` (`GoldFishRunner.cpp:~2052`):
```
|| d->params.land_play_damage_after_first > 0     // Fastbond
|| d->params.lose_life_on_resolve > 0             // Grim Tutor
|| d->params.set_all_life_totals >= 0             // Worldfire (-1 = absent)
```
- **No other deck moves.** These params are new and no other list holds these cards.
- City of Brass alone still does not arm own death. The decks that hold it (Gods, Creature Giving,
  Maze's End, Melira Pod, Minotaur, Prevent Damage, Unpredictable Cyclone, Extra Turns, Fungus
  candidate-b) are unchanged.

Every consumer respects it. The search, rollout and d0 already read `OpponentHasLost` / `SelfHasLost`.
The go-off recognizer's life check (§5.2) and the Fastbond count axis (§6) carry it explicitly: **a
chain that kills us is not a win.**

---

## 8. The Emrakul loop: what exists, what is missing

**What exists (verified in §1.4):**
- the cast trigger gives an extra turn in both worlds;
- the round does not advance;
- extra turns are searched at full depth;
- the legend rule is a state-based action, with a searched keep;
- the legend death goes through `OnCreatureDies`, so the graveyard shuffle fires and returns the spent
  Revelations and Time Warps to the library;
- Revelation can then find Emrakul again.

**15-mana planning:**
- Payment of {15} generic from 3- and 4-mana lands goes through the one-colour-burst and single-colour
  N paths (§1.5).
- Unit tests in step 2 pin: four Ruins pay {15}; one Lotus tap cannot pay {U}{G}; two Lotus taps pay
  {U}{U}{G}.
- Watch `PrepayWildTrue`'s 8 × 8 overflow at 20+ mana (R3).

**Missing, step 7:**
1. **`DuplicateCastHasUpside` → true when `def.params.cast_trigger_extra_turns > 0`.** The cast trigger
   resolves before the spell, and even if the spell dies (Emrakul ruling 1), so the duplicate is never
   the tie the prune assumes.
2. **`DuplicateEntryOrDeathHasUpside` → true when `def.params.put_into_graveyard_shuffles_graveyard` and
   the graveyard holds a card.** The legend-rule death shuffles the graveyard back.

Both changes sit BELOW the d0 gate, as the USER's 2026-08-29 and 2026-11-18 rulings require. **Shared
card fix:** Extra Turns and Unpredictable Cyclone hold Emrakul, so their SEARCHED play can move when two
Emrakuls meet. Call it out, and accept GT through the normal flow.

**Fold F-E1, sound: keep the OLDER copy.** For a `vanilla_creature` legend whose copies differ only in
having entered this turn (no counters, damage or attachments differ), keeping the new copy is dominated.
Both keeps give the same death event and the same shuffle, and the older copy can attack now. This
removes the keep twin that the LEGEND-KEEP pass emits.

**Annihilator 6:** existing (`ApplyAnnihilator`), and inert for the win turn.

---

## 9. The other new cards

### 9.1 Grim Tutor
- **Params:** `tutor_to_hand: true`, `tutor_types: []`, `tutor_shuffle_after: true`,
  `lose_life_on_resolve: 3` (new; generic). The life loss is applied right after `PerformTutor` in both
  worlds, through `dmgev` life loss: it is not damage, so prevention does not apply. Ruling: the card is
  not revealed (inert).
- **Overlord whitelist.** Add `lose_life_on_resolve` to the audited-param whitelist
  (`CardDatabase.cpp:148-158`), or the upkeep-sacrifice fold silently turns off for states that hold the
  card.
- **Target.**
  - The target is SEARCHED. `BloodSunProvider::TutorSearchWidth()` returns the number of distinct
    names, which makes it complete.
  - Grim Tutor uses the same cast-time encoding as Revelation (one cast `Action` per class, the target in
    `tutor_target`; the Transmute / Tooth and Nail shape) through a provider opt-in hook,
    `TutorCastTimeAxis()`. That closes the one-tutor-per-plan gap for this deck without touching other
    decks.
  - The executor already honours `entry.tutor_target`.
- **Viewer:** `tutor` (bucket A).

### 9.2 Dimir Machinations
- **Params:** `look_top_target_player_exile_reorder: 3` (new), `targeting: "player"`,
  `transmute_cost: "{1}{B}{B}"` (existing).
- **Resolution.** A new primitive, `LookTopExileReorder(state, player, n)`, on the TARGET player's
  library, in both worlds. A new `LookKind::ExileThenReorder` feeds `EnumerateTopDispositions`. For 3
  cards there are 16 dispositions: Σ_k C(3,k)·k!.
- **Fold M1, sound:** identical names in the look fold their permutations (copies are interchangeable;
  real-information identity).
- **Search.** A disposition pin (the Ponder `g_scripted_reorder_choice` shape) with **every**
  disposition offered. The engine-side `MTG_SCRY_WIDTH = 2` cap must not apply to this card, because
  that constant truncates. The provider orders by heuristic: keep the lands and engines on top, exile
  dead cards.
- **Self target.** The look is honest information: the player sees the three cards, so it is not
  clairvoyance abuse. A later Revelation or Grim Tutor shuffle erases the reorder.
- **Opponent target is NOT inert.**
  - The goldfish opponent plays lands from its library every turn, and each one is a Burgeoning trigger
    for us. Exiling or reordering its top three changes when it plays lands.
  - It also adds three cards toward Ulamog's deck-out.
  - Add the param to `DeckTouchesOpponentZones` (`GoldFishRunner.cpp:635`), so the opponent library is
    read card by card (`opponent_library_read`).
  - The target-player axis is the Glimpse `Action::player_target` precedent (`TurnSolver.cpp:~54652`).
  - Recommendation: implement both targets, and have the provider offer the opponent target in the LAST
    wave (U3).
- **Transmute.** Existing. The mana value 3 targets in this list are Blood Sun, Grim Tutor and
  Machinations. Viewer: `tutor`.
- **Viewer:**
  - **target player:** `target` with a players-only legal set (the Heroes' Reunion shape, bucket A);
  - **the look:** `lookPanelHtml` with the new kind `exile_reorder`. It is the `surveil` shape with
    destination "exile" instead of "graveyard": an extension of the existing `scry`/`surveil`/`reorder`
    type. A new `kind` value on `WriteTopDecisionJson`, plus a GUI branch.

### 9.3 Worldfire
- **Params** (all new, generic):
  - `exile_all_permanents: true`;
  - `exile_all_hands_and_graveyards: true`;
  - `set_all_life_totals: 1`.
- **Resolution order** (ruling 1): exile all permanents (both sides, including opponent spawns and land
  tokens), then all hands and graveyards. Then each life total becomes 1. That counts as life loss for a
  player above 1 (ruling 2), and the opponent's loss counts toward its total. No trigger resolves in
  between. Worldfire itself goes to the graveyard last (ruling 3).
- **Emrakul exiled is NOT put into a graveyard, so there is no shuffle.** Pending extra turns survive,
  because they are not objects.
- **Afterwards:**
  - Recompute `bonus_land_drops_this_turn` (§6).
  - Our life is 1: with Blood Sun gone, a later City of Brass tap kills us. The search sees this through
    own death.
  - The opponent at 1 needs any 1 damage. **In this list nothing deals damage except attacking
    creatures, and Worldfire exiles them.** No goldfish line found here makes Worldfire speed up a win:
    rebuilding 10+ mana for Ulamog or 15 for Emrakul from an empty board takes many turns.
- **Search.** It is cast-enumerated normally (it is a plain sorcery), ordered LAST in every menu, and
  last in the Revelation order. A restriction is the USER's call (U2).
- **Viewer:** no choices.

### 9.4 Blood Sun itself
- **Params:** `lands_lose_nonmana_abilities: true`, `etb_self_draw: 1`. Template `custom` enchantment;
  `mana_cost "{2}{R}"`.
- **The draw.** Step 3 must confirm that `etb_self_draw` fires for an enchantment from
  `FireOwnEtbTriggers` in both worlds. The precedents are artifacts (Ice-Fang, Astrolabe, the snow
  rock). The draw arms the put-in-hand breakpoint (site 10's in-hand arm, or the draw site), so the drawn
  card is re-solved with the turn's remaining mana.
- **Viewer:** no choices.

### 9.5 Time Warp, Exploration, Burgeoning, Ulamog (present)
- **Time Warp** is unchanged. Its `targeting: "player"`: confirm in step 8 that the opponent target is
  not offered. If it is offered, the provider orders it last. It is a provider restriction to disclose,
  since the opponent's extra turn gives us at most one Burgeoning put.
- **Exploration** is unchanged (cumulative).
- **Burgeoning** is unchanged. The opponent takes no turn inside an extra-turn chain, so Burgeoning is
  silent during the go-off.
- **Ulamog** is unchanged.

---

## 10. Viewer (2c-ter), second main (2c-bis), provider (4a)

### 10.1 Viewer decisions per card
| Card | Choice | Bucket | Mechanism |
|---|---|---|---|
| Blood Sun | none (draw is forced) | — | — |
| Lotus Vale / Scorched Ruins | which two untapped lands to sacrifice (no Blood Sun) | A | `sacrifice` ×2 (Lake of the Dead precedent), only untapped lands legal; one legal pair = forced |
| Lotus Field | which two lands (tapped allowed) | A | `sacrifice` ×2; F-L1's tapped-first pick preselected |
| Golgari Rot Farm / Rakdos Carnarium | bounce target (no Blood Sun) | A | `bounce` |
| Copperline Gorge / Blooming Marsh / Graven Cairns / City of Brass | none (filter colour is paid by pips) | — | — |
| Diabolic Revelation | X, then up to X cards | A | `x_value`, then `dragon` multi-pick (§4.5) |
| Grim Tutor | target | A | `tutor` |
| Dimir Machinations | target player; the exile and the order | A (extension) | `target` (players-only); `reorder`-family `kind: exile_reorder` (§9.2) |
| Dimir Machinations transmute | target | A | `tutor` (row "Dimir House Guard's Transmute") |
| Fastbond | how many lands this turn | A | `main_phase` plans (one land per click, as today) |
| Worldfire | none | — | — |
| Emrakul ×2 | which copy to keep | A | `legend_keep` (the old copy preselected, by F-E1) |
| Time Warp | target player | A | `target` players-only, if both are offered |

- No bucket-B type is needed. The one new `kind` value (`exile_reorder`) extends an existing type and its
  panel.
- Each wiring is at the SHARED resolution site: `PerformTutorPackageToHand`, `LookTopExileReorder`, the
  sacrifice choosers inside `PlayLandCore`/`EnterLand`.
- Add the DECISIONS.md rows and the param → type mapping in `scripts/audit_viewer_decisions.py`. Stage 5h
  forces each one in a scenario.

### 10.2 Second main (2c-bis)
**Not relevant.** Every spell is a sorcery-speed setup piece, and no card creates a resource in combat.
Combat matters only through Emrakul's attack (and Ulamog's), and casting before or after it does not
change the win turn: a creature cast precombat cannot attack, and the keep fold retains the attacker. No
`DeckUsesSecondMain` term is needed. Disclose this in 6a.

### 10.3 Provider
**Today the deck routes to `GenericProvider`. After step 5 it would route to `AntiLifegainProvider`,
through `tutor_to_hand`.**

**`class BloodSunProvider : public DeckProvider`.** It derives from `DeckProvider`, which is the class
Generic's deck-level siblings extend (the Stuffy Doll precedent). It does not derive from `ExtraTurns` or
`Cyclone`: those are thematic siblings with their own ranked behaviour.

**Signature:** `p.lands_lose_nonmana_abilities`, which is Blood Sun's own param and unique. It is routed
in the top block, above `anti` and every tutor-keyed signature, in the `stuffy` position.

It is created in step 3, the first step that adds a param, so the deck never measures under a borrowed
provider.

**Hooks:**
- `GoOffPackages` + `RevelationPackageOrder` (§5);
- `TutorSearchWidth()` = all;
- `TutorCastTimeAxis()` = true;
- `ExtraTurnChainWalkEnabled()` = true (step 8; U8);
- the Machinations disposition order;
- the case-3 wave classifier (§5.4; default off, U4);
- effective-`enters_tapped` reads in its keep and discard ranks;
- `CastOrderRank` (USER-OWNED; present only).

**Certificate:**
- It returns `NotAssessed` first: the turn-scoped certificate is inapplicable under `deck_has_extra_turn`.
- `ProvenNoWinThisRound` is added in step 9 as a one-sided proof.
- `scripts/provider_audit.py --check` requires a per-class `Certificate()`, which this satisfies.
- The audit needs a `*.profile.json` in the deck folder, so it is run at Stage 4a.

---

## 11. Risks, rules items, performance

**Rules items to verify:**
- **R1.** Does Fastbond trigger on a Lotus Vale "played" into the graveyard? CR 305.1 says the special
  action happened; Fastbond's ruling 1 says "You take damage when you play a land". Fold V1 makes it moot
  for autonomous play. A human could still do it, so the implementation must pick a reading. Recommended
  reading: it triggers, since the land was played. Check `mtg-rules.md`.
- **R2.** Machinations "target player": an opponent target with fewer than 3 cards looks at what there is.

**Risks:**
- **R3.** 20+ mana pools: the `PrepayWildTrue` 8-slot / 8-unit overflow, and the backtracker's
  colour-pair branching across five one-colour bursts. Profile the step-7 scenario.
- **R4.** The unbudgeted backstop rows (budget 0, d3 and play depth) drain the Revelation generator
  wherever no win at R+1 is reached and `ProvenNoWinThisRound` is weak. Measure before suite admission.
  If degenerate, the case-3 hard restriction is U4.
- **R5.** Clairvoyance. Package choice also selects the post-shuffle draws. That is the engine-wide
  known-library simplification. Report the `MTG_NC_SEARCH` bracket at Stage 5.
- **R6.** The 3x cost rule (Hinata yardstick). The deck's cost is unknown until steps 6–8 land, and
  extra-turn chains multiply per-round work. The go-off front and the B&B are the containment. Measure
  `suite_gate.py --cost` before admission.

**Branching hot spots and their containment:**
| Hot spot | Containment |
|---|---|
| Revelation packages (§4.3) | lazy generator; go-off front; B&B stops generation at a round-R win; `ProvenNoWinThisRound` for the strictly-earlier test; budget waves (inert at unlimited); folds F-R1..F-R4; case-3 last wave |
| Fastbond drops (§6) | land-batch normal form LB; count axis max-first; LB2 partial-order dominance; class folds (G8) |
| Lotus sacrifices (§3) | class multisets; V1, F-L1, F-L2; under Blood Sun none |
| Machinations dispositions (§9.2) | 16 per look; M1 copies fold; opponent target in the last wave |
| Grim Tutor (§9.1) | one cast per class, about 19 |
| Extra-turn chains (§5) | go-off front at every turn + B&B, so linear in loop length; chain walk witness; `TurnSeqCapped` |
| Legend keep (§8) | F-E1 removes the twin |

---

## 12. Implementation plan

Every step:
- builds with `./build.sh` and passes `test/scenarios.sh` and the named unit and scenario tests;
- is reviewed against `.claude/skills/mtg-rules.md` before commit (2d);
- passes `scripts/` cost audit 2d-bis for its cards.json entries;
- leaves smoke **play-neutral for every other deck**: every non-Blood-Sun smoke digest is unchanged,
  except where a step says otherwise.

The Blood Sun deck is not in the suite until Stage 4-bis, so its own digests are not a gate yet. Each
step writes the cards.json entries for the params it adds. Steps are sized for one Opus implementer well
under 250K context. Subagents do not push; they commit locally on `bloodsun-analysis`.

**Step 1: Tier-1 lands.**
- Work: cards.json entries for Golgari Rot Farm, Copperline Gorge and Graven Cairns (§3.3).
- Tests: scenarios for each:
  - Rot Farm bounces and enters tapped; Rot Farm alone bounces itself;
  - Gorge is tapped with 3 other lands and untapped with 2;
  - Cairns' filter pays {B}{B}, and its {C} mode pays generic.
- Smoke: neutral (new names only).

**Step 2: Lotus Vale, Scorched Ruins, Lotus Field (no Blood Sun yet) + the big-mana payment tests.**
- Work:
  - params `etb_replace_sacrifice_untapped_lands`, `etb_sacrifice_lands`;
  - the replacement in `PlayLandCore` and `EnterLand` (NO float-before-sacrifice);
  - Field's trigger through `ResolveLandEnterEtbExtras`, with float-before-sacrifice and the "fewer than
    two others → all" rule;
  - the searched pair axis and the `sacrifice` viewer wiring;
  - folds V1, F-L1, F-L2;
  - cards.json for all three.
- Tests:
  - unit payer tests: 4 Ruins → {15}; a Lotus can't pay {U}{G} in one tap; two Lotus taps pay {U}{U}{G};
    a 22-mana pool through `PrepayWildTrue`;
  - scenarios: Vale with 1 untapped land goes to the graveyard and spends the drop (human path only);
    Field taps two lands, then sacrifices them; Field with one other land sacrifices both.
- Smoke: neutral.

**Step 3: the Blood Sun predicate (generic G1–G9) + Blood Sun + `BloodSunProvider` skeleton.**
- Work:
  - `lands_lose_nonmana_abilities`, the stamp, `LandNonManaAbilitiesSuppressed`;
  - every G-site in §2.3;
  - `tap_self_damage_is_trigger` on City of Brass (**shared card**; read only under suppression, so it is
    byte-identical elsewhere);
  - the `ManaCacheKey` bit; `land_sig` effective; `EffectiveLandIdentity` + `mana_only_identity`;
  - the §2.4 self-guard with its classification unit test;
  - the opponent-land entry;
  - Blood Sun's cards.json, including confirming `etb_self_draw` on an enchantment and its breakpoint;
  - provider class, routing and `Certificate() = NotAssessed`.
- Tests:
  - scenarios for every row of §3.4 under Blood Sun;
  - Blood Sun cast mid-turn: a City tapped after it does not ping; a Karoo that entered tapped before it
    stays tapped;
  - Worldfire is not built yet, so "Blood Sun leaving" is tested with a synthetic exile in the unit test;
  - a self-guard abort test with a synthetic fetchland in the list;
  - the opponent's Karoo enters untapped.
- Smoke: neutral. Confirm City-of-Brass decks are byte-identical.

**Step 4: Fastbond + own-death arming (D1).**
- Work:
  - `unlimited_land_plays` (kUnlimitedDrops), `land_play_damage_after_first` in `PlayLandCore`;
  - the loop-reader audit (`guard < 8` → lands in hand);
  - drop recompute when a drop-maker leaves;
  - the `own_death_live` terms (§7; Grim and Worldfire terms land with their params, but the code shape
    goes in now);
  - cards.json for Fastbond.
- Tests (scenarios):
  - 4 lands with Fastbond → 3 damage;
  - a Burgeoning put → 0;
  - 2 Fastbonds → 2 per play;
  - life 3 with 5 lands in hand: the search holds lands and survives;
  - Exploration + Fastbond: still 1 damage per non-first play.
- Smoke: neutral.

**Step 5: Grim Tutor, Dimir Machinations, Worldfire.**
- Work:
  - `lose_life_on_resolve` (+ Overlord whitelist);
  - `TutorCastTimeAxis` + `TutorSearchWidth` overrides;
  - `look_top_target_player_exile_reorder` + `LookTopExileReorder` + `LookKind::ExileThenReorder` + the
    disposition pin (full width) + fold M1 + the `DeckTouchesOpponentZones` term + the `exile_reorder`
    viewer kind;
  - Worldfire's three params and its resolution order (§9.3);
  - cards.json for all three.
- Tests:
  - Grim Tutor at 3 life kills us with own death live, so the search does not cast it;
  - Machinations on self: every one of the 16 dispositions reachable;
  - Machinations on the opponent: the opponent's land play moves;
  - Worldfire: Emrakul exiled without a shuffle; both life totals at 1; pending extra turn kept; drops
    recomputed.
- Smoke: neutral.

**Step 6: Diabolic Revelation, the package axis.**
- Work:
  - `tutor_up_to_x`; the {X}-block branch; the Action package field;
  - `PerformTutorPackageToHand` (one shuffle) in both worlds;
  - `RevelationPackageGen` (lazy; folds F-R1..F-R4; canonical dedup);
  - the provider order hook with the §5.4 ordinary key;
  - per-package `ChargeDecisionUnits` waves;
  - the generator-stop on `pass_best_win == turn_number`;
  - human `x_value` + `dragon`;
  - cards.json.
- Tests:
  - unit: generator yields each multiset exactly once on a small library (count against the closed
    form);
  - unit: Vale ≡ Field under Blood Sun only;
  - unit: no package after a round-R win (an instrumented counter);
  - scenario T2 (USER G7 4-drop, §5.3) wins at round R even with ordinary order, small library.
- Smoke: neutral.

**Step 7: the Emrakul loop.**
- Work:
  - `DuplicateCastHasUpside` / `DuplicateEntryOrDeathHasUpside` terms (§8; **shared**: Extra Turns and
    Unpredictable Cyclone searched play may move; d0 unchanged);
  - fold F-E1.
- Tests (scenarios):
  - Emrakul on board + 22 mana + Revelation in hand → win at round R through the loop, with the legend
    keep = old copy and the graveyard shuffle returning the Revelations;
  - 21 mana with one Revelation in library → the Claim-L line.
- Smoke: Extra Turns / Cyclone may show `play-changed`. Read each one; accept smoke and regression GT
  through the normal flow, stating the shared fix.

**Step 8: the go-off recognizer (stages 1 + 2) and the chain walk.**
- Work:
  - `GoOffPackages` with the §5.2 resource model (remaining drops, purchasable drops, colours, net of
    hand, multiple Explorations, own life) and the stage-2 two-turn lookahead rank;
  - entry 0 to d0 and the playout;
  - `ExtraTurnChainWalkEnabled()` (measure, U8).
- Tests (scenarios; each asserts a win at round R and a unit ceiling):
  - **T1:** the USER G4 worked example;
  - **T1b:** the Fastbond variant with no open drop ({G} held);
  - **T1c:** the same with Time Warp already in hand (G5: X = 4);
  - **T2:** the G7 4-drop;
  - **T2b:** 3 Ruins + a Lotus;
  - **T2c:** the drops bought inside the turn (G8);
  - **T3:** a bare Time Warp in hand with no Revelation → not a go-off (order unchanged);
  - **T4:** life 2 with a Fastbond line that would kill → not taken.
- A/B: held-out seeds, Blood Sun only (other decks are untouched by construction).

**Step 9: `ProvenNoWinThisRound` + `Certificate()`.**
- One-sided. Labels byte-identical (verify), speed only.
- The unbudgeted backstop probe (R4).

**Step 10: the case-3 prune as a last wave (U4).**
- Default off.
- A/B against the USER's prune bar: better overall, no unrecognisable lost lines (diff the game logs),
  faster.
- The default goes to the USER.

**Step 11: Stage 5h viewer surface check.**
- Force every §10.1 row in a scenario through the real protocol.
- DECISIONS.md rows + the audit manifest.
- (Each card step already wired its chooser; this step proves they fire.)

After step 11 the analysis continues per `analyze-deck.md`: Stage 3, Stage 4 baseline profile, 4-bis
suite entry (3x rule), 4a routing, and Stage 5.

---

## 13. Open USER decisions (surfaced; work proceeds on the stated default)

- **U1, own death. DECIDED (D1, 2026-11-20).** Live for this deck, armed from Fastbond's, Grim Tutor's
  and Worldfire's own params. No other deck moves.
- **U2, Worldfire.**
  - Finding: I found no goldfish line in this list where Worldfire speeds up a win. It exiles every
    attacker, and nothing else deals damage.
  - Question: how do you use it?
  - Default: model it faithfully, order it last everywhere, and do not restrict it.
  - Recommendation: if no line exists, a provider restriction (no Worldfire cast and no Worldfire in a
    Revelation package unless a creature can attack afterwards), subject to the prune bar.
- **U3, Dimir Machinations targeting the opponent.**
  - Finding: it is not inert. It moves the opponent's land plays, and so Burgeoning, and it adds toward
    Ulamog's deck-out.
  - Default: both targets implemented, with the opponent target in the last wave.
  - Recommendation: keep that. A hard restriction to self would be lossy.
- **U4, the case-3 prune ("we will probably still heuristically prune the regular search case").**
  - Default: build it as a LAST-WAVE restriction. That keeps completeness at large budget and acts as a
    prune under the shipped budget. Ship it OFF until the A/B.
  - Recommendation: adopt it if it meets your prune bar.
  - Open question: if the unbudgeted backstop rows still blow up (R4), do you want a HARD restriction?
    That is lossy at unlimited budget.
- **U5, the residual gap in "(2) always bridges to (1)".**
  - Under Blood Sun, mana never falls across extra turns. But the bridge is bounded by the 2 Time Warps
    and 4 Revelations, which come back only through an Emrakul shuffle. It also needs enough growth from
    the fetched lands and enough colour, {U}{U} only from City or Lotus.
  - A minimal bridge (X = 2, no ramp) can stall below 21 mana. A bridge without Blood Sun can lose mana.
  - Default, per your ruling: stage 2 is in the go-off as an ORDER. Packages whose two-turn lookahead
    reaches the Emrakul stage come first. It becomes a proof only when the engine's line actually wins
    this round.
  - Question: is that the treatment you want for the gap cases?
- **U6, the shared Emrakul fix (§8).**
  - Offering a second Emrakul in searched play (d0 unchanged) can move Extra Turns' and Unpredictable
    Cyclone's searched play when two Emrakuls meet.
  - Default: do it; it is a modelling fix (the cast trigger is real upside) and goes through the normal
    accept flow.
- **U7, X = 0 and copy folds are real-information identities.** They are dominated except through
  clairvoyance (the `LandSearch.h` standard). Default: fold, and disclose in 6a.
- **U8, the depth-0 chain walk for this deck.**
  - Enabling `ExtraTurnChainWalkEnabled()` also brings in the Extra Turns deck's tighter priced-decision
    limit (`DecisionPricedLimitFor`, which keys on the same hook).
  - Default: enable the walk, and decouple the priced limit onto its own hook, so this deck keeps the
    generic limit.
  - Recommendation: measure both in step 8.
- **Cast order, mulligan policy and discard buckets** are USER-OWNED. They are not designed here, and
  will be authored and presented at Stage 5.

---

## 14. Handoff
Steps are executed one fresh agent per step, from this document. Each implementer appends a short
"Implementation notes (step N)" section below this one: commit hash, what deviated, measurements. That
follows the `damage-reflection-pipeline.md` §14 convention.

### Implementation notes (steps 1-2)
**Step 1** (commit `f86b28103`): cards.json for Golgari Rot Farm, Copperline Gorge, Graven Cairns in the existing shapes
exactly (§3.3). Seven scenarios (`test/scenarios/bloodsun_rotfarm_*`, `bloodsun_gorge_*`, `bloodsun_cairns_*`). No
engine change. Cost audit 2d-bis: N/A (lands, empty cost; the audit skips them).

**Step 2** (commit: see `git log --grep "Blood Sun step 2"`):
- Params `etb_replace_sacrifice_untapped_lands` (Vale, Ruins) and `etb_sacrifice_lands` (Field) in `CardParams`;
  `LandLotusSacrificeCount` reads either.
- **The searched axis is the existing LAND-ENTRY PIN** (`Plan::rad_mode` / `LandPlayOptions::rad_mode` on a drop,
  `g_land_entry_pin` on a put; Maze's End I2). `LandHasEntryPinChoice` now includes the Lotus lands, and the base
  `DecisionProvider::LandEntryPinCandidates` returns one index per entry of the menu (`LotusSacrificePicks`). This
  reuses every route that already threads the pin (deferred drop, Karoo-position, breakpoint continuation, executor,
  Burgeoning's crossing pin, land-tutor puts), and each pin is its OWN land option, so the casts are enumerated on
  the board that sacrifice leaves (never re-pinned afterwards). No new `Plan` field.
- **The menu** (`LotusSacrificePicksImpl`, SpellEffects.h after `SacLandTwins`): every multiset of N victims over
  land classes (identity fold = `SacLandTwins`), lexicographic over the provider's NEW `LotusSacrificeRanking`
  hook (ORDER only; base = lowest per-tap yield first, then `SacrificeLandCandidates`' tapped-first order -- with
  the plain tapped-first ranking the front pick sacrificed the just-entered, tapped Field itself). Vale/Ruins: untapped lands only; Field: every land, itself included; F-L1 = tapped copies
  first within a class. With <= N eligible lands there is no choice (Vale: exactly N -> both, fewer -> fails;
  Field -> every land, the ruling). Width bound before the drop (`LotusSacrificePickCount`): the Field counts itself
  as one extra class (never below the truth; an over-wide pin clamps, the duplicate-not-whiff rule).
- **Vale/Ruins replacement** in `PlayLandCore` (beside the Lake's, before the land exists; on failure: graveyard,
  drop spent, `DrawForPlayedLand` as for the Lake) and in `EnterLand` (after the pin is taken, before the permanent
  is built; on failure the card goes to its owner's graveyard and nothing enters). No tap-first float.
- **Field trigger** in `PlayLandCore` (right after the Karoo bounce step, re-found by number) and in
  `ResolveLandEnterEtbExtras` (new defaulted `entry_pin` arg; `EnterLand` passes its pin through). Tap-first float
  of every untapped victim (`FloatLandBeforeSacrifice`, "in response to the sacrifice"), then the sacrifices,
  highest index first. **Deviation:** `PerformLandTutorToBattlefield` keeps `resolve_full_etb = false` (the Creature
  Giving Karoo reason), so the Field's trigger is resolved there explicitly, param-gated.
- **Folds V1 / F-L2** extend `LakeDropWithoutVictim` (every autonomous land-choice site already calls it; the
  enumeration exempts human play). The "generic upside" checks: V1 stands down under an active Horn
  (`ActiveHornCount`: the land IS played); F-L2 also under `deck_has_landfall`, `deck_has_revolt` or an own
  `sacrifice_watch_pump_power` permanent (the Field DOES enter and lands ARE sacrificed).
- Hand-land signature (`land_sig`): `lv<N>` / `lf<N>` (+ `o1` for one-colour production) so a Lotus never folds with
  a plain land. Plan label `[sacK]`; claude-play menu note "(sacrifice pick K)".
- Viewer: two `sacrifice` board clicks (`ChooseLotusVictims`, each over every remaining eligible land, unfolded;
  the menu's front pick as the defaults); `tools/play/DECISIONS.md` row; `scripts/audit_viewer_decisions.py` maps
  both params to `sacrifice`.
- **Known limits (disclosed, not fixed here):** (1) Burgeoning's crossing pin caps a decision at 32 ranks
  (`kCrossingLandPinBits`), so a Lotus put by Burgeoning with > 32 menu entries loses the tail -- unreachable under
  Blood Sun (no sacrifice at all) and with the deck's land count; (2) a SIMULTANEOUS multi-put (Genesis Wave /
  Manabond's dump) resolves a Field's trigger only where that put's second pass calls `ResolveLandEnterEtbExtras`
  (unpinned, the front); neither card is in the Blood Sun list; (3) R1 (Fastbond and a Vale played into the
  graveyard) is step 4's; the Vale failure path already calls `DrawForPlayedLand`, i.e. treats it as PLAYED.
- (cont.) (4) a Lotus land found by a LAND SEARCH put (`LandSearch.h`: Primeval Titan, sideboard only) takes the menu's
  front -- the land-search pick pin does not carry an entry pin; (5) declining the Vale's sacrifice while two
  untapped lands exist is collapsed, as the Lake's (it is the graveyard outcome with the lands kept -- no better
  than holding the Vale; fold V1's argument), so the human is not offered it either.
- Scryfall snapshot: the six new lands were fetched into `src/cards/data/scryfall_reference.json` (appended in the
  file's order; no other entry touched). `audit_card_fields.py` rc 0 (oracle-text divergence advisory only: the
  bracket notes).
- **Measurements (step 2 tree):** `mtg-test` 3790 cases, 3789 pass -- the one failure is PRE-EXISTING and not ours:
  `Pirates routing: every deck in decks/ resolves exactly as before` counts 61 decklist folders against 60 pinned,
  because `decks/Blood Sun/` (ledger stage) has no routing row yet; step 3 adds `BloodSunProvider` and its row.
  New `test_bloodsun_lotus.cpp`: 15 cases pass (menu, folds, drop/put resolution, 4 Ruins -> {15}, one Lotus
  cannot pay {U}{G}, two Lotus pay {U}{U}{G}, a 22-mana Emrakul + Titan + Mystic line through
  `BatchPrepayMainCasts` and back out with 0 left -- R3's 8-slot overflow did not bite on this line).
  `test/scenarios.sh` 600/600 (10 new `bloodsun_*`). Smoke: 235/235 PASS, **0 moved digests** vs
  `build/scratch/bloodsun/impl0/smoke-step0-batch.log` (saved as `impl2/smoke-step2-batch.log`).

### Implementation notes (step 3)
**Commits:** WIP `f25409835` (implementer 1: everything below except the last bullets) + the step-3 finishing commit
(`git log --grep "Blood Sun step 3:"`: tests, scenarios, snapshot, viewer classification, these notes).
- **What landed** (handoff `logs/bloodsun/impl-step3-handoff.md` lists every site): params
  `lands_lose_nonmana_abilities` (Blood Sun) and `tap_self_damage_is_trigger` (City of Brass); the single land-param
  classification `src/cards/LandAbilityClasses.h` (Mana / Gated / SelfGuard; unclassified = SelfGuard); the load-time
  projection (`CardDefinition::land_ability_loss_twin`, `mana_only_identity`, `land_ungated_ability`); the stamp
  `GameState::deck_has_land_ability_loss` (default TRUE unstamped, stamped `garth || Blood Sun in main/side`); the
  predicate + `EffectiveLandDef` / `EffectiveLandIdentity` / `EffectiveTapSelfDamage` (`src/core/LandAbilityLoss.h`);
  G1-G9 as §2.3; the Karoo deferral judged on the plan START state (under Blood Sun a Karoo is a plain land, played
  first) in every world; the §2.4 self-guard (`[bloodsun] ... gate it before playing this list`, exit 3);
  `BloodSunProvider` (NotAssessed, routed right after `stuffy`, i.e. above AntiLifegain); cards.json Blood Sun.
- **Deviation 1 (§2.3 G2):** depletion / mana-counter lands (`enters_tapped_with_depletion`, `enters_with_mana_counters`,
  `mana_counters_are_mining`, `sacrifice_when_mana_counters_empty`), `etb_choose_color` and the colour/type choice
  lands, and Cavern / Unclaimed Territory (`colored_creature_*`) are SELF-GUARD, not gated: dropping their entry would
  leave a mana ability the engine still models as working with no counter / chosen colour. None is in the list.
- **Deviation 2 (G4):** the self-guard does NOT scan the opponent's land pool (the passive opponent models only
  enters-tapped and fetch, both gated; scanning would abort on its fetchlands).
- **Addition (sound widening, param-gated):** the land-POSITION axis is opened for a Blood Sun list —
  `LandOrderMatters` also returns `deck_has_land_ability_loss`, `PutsLandfallWatcher` counts a
  `lands_lose_nonmana_abilities` cast, and `DecisionProvider::KarooEntryObserver`'s base returns true for it — so
  "land before / after Blood Sun" is searched. It only offers more lines (costs branching for this deck only).
- **Disclosed, not gated:** the payment ORDER heuristics that read `tap_self_damage` (pain-last ordering in
  `ManaPayment.cpp`) and the armed-only pricing (`PaymentDamageLowerBound`) — the standing mana-payment exemption; the
  deck is not `dmg_events_armed`.
- **City of Brass identity:** the new param changes City's DefHash / `behaviour_identity`. Play elsewhere is
  byte-identical (read only under suppression; smoke below). The artifact queue's card-data axis
  (`artifact_queue.py` `canon_card`: the whole entry minus name/oracle_text) will flag `card-data` on the three shipped
  City decks — Creature Giving, Gods, Prevent Damage (each has a keep table + value leaf) — at FLAG_WEIGHT 0.0
  (reported, not scored). Keep-journal provenance (`ExhaustiveKeep` `card_defs`) will attribute City as a changed card
  on a regen re-run; no play or load gate reads it.
- **THE KAROO GAP (parent ruling: root-cause it).** The top-level enumeration plays a deferred Karoo after the plan's
  casts and enumerates those casts on the PRE-Karoo board (`add_for_land`), and the Karoo-position clones place it
  before an observer cast, i.e. BEFORE Blood Sun; so no TOP-LEVEL plan holds "Blood Sun, Karoo, then a cast paid by
  the Karoo". The line is nonetheless findable, through Blood Sun's own draw: `etb_self_draw` arms the site-10
  put-in-hand breakpoint (`MTG_BP_PUT_IN_HAND`, default on), ApplyPlanDirect plays the deferred Karoo after the main
  casts and BEFORE the deferred re-solve (AIEngine::TakeTurn the same), the Karoo now enters as its projection
  (untapped, no bounce), and the continuation is enumerated on that board. Pinned by
  `bloodsun_sun_then_karoo_untapped_funds_cast.json` (budget 0) and `bloodsun_sun_then_karoo_budgeted.json` (d3 b100):
  the only turn-3 kill is Blood Sun -> Gruul Turf untapped -> Bolt, Bolt; with `MTG_BP_PUT_IN_HAND=0` it wins on turn 4
  (route proof). No engine change. Residual boundary (stated, not fixed): the route rides the draw, so it needs a
  non-empty library and runs at sink depth 0 / autonomous play; inside a continuation the re-solve is inline (the
  nested continuation enumerates its own land drop on the post-Blood-Sun board), and a human orders it by committing
  Blood Sun first (commit-the-line re-asks on the new board).
- **Blood Sun's ETB draw fires for a cast enchantment in both worlds** (§9.4): the human scenario
  `bloodsun_sun_etb_draw_casts_drawn.json` casts the drawn Bolt; the autonomous Karoo scenario's kill needs the drawn
  Bolt (rollout finds it, executor plays it).
- **Rules review (2d):** City tapped to PAY for Blood Sun still pings (Blood Sun is on the stack, CR 603.2 + City
  ruling 3) — `bloodsun_city_pays_for_sun_pings.json` (20 -> 19, executor). Painland damage is part of the mana ability
  and stays (Mana class). Rulings 5-6 (granted abilities, type-changing lands) are self-guard items; no card in the
  list grants a land an ability. Blood Sun's draw resolves at once (goldfish: nothing can respond).
- **Test fixes:** two pre-existing unit tests that hand-clear the land-position stamp bits now also clear
  `deck_has_land_ability_loss` (`test_landfall.cpp` "a deck with no landfall card...", `test_cyclone_sound_folds.cpp`
  Q36): its default-TRUE unstamped value re-opened the axis in those hand-built states. Real games are stamped.
- **Cost audit (2d-bis):** `audit_card_costs.py` on Blood Sun: rc 0. Scryfall snapshot: Blood Sun appended to
  `scryfall_reference.json` (no other entry touched); `audit_card_fields.py` rc 0 (oracle bracket note advisory only).
- **Viewer:** Blood Sun has no choices (no DECISIONS.md row). `audit_viewer_decisions.py` classifies
  `lands_lose_nonmana_abilities` and `tap_self_damage_is_trigger` as INERT_PARAMS (USER sign-off to collect);
  `--no-sweep` rc 0 (the deck has no profile yet, so the dynamic sweep cannot run).
- **Measurements (step 3 tree):** `mtg-test` 3804 cases, 3804 pass (the step-2 `Pirates routing` failure is fixed by
  the Blood Sun row). `test/scenarios.sh` 610/610 (10 step-3 `bloodsun_*`: 6 by implementer 1, 4 here). Smoke: 235/235
  PASS, **0 moved digests** vs `impl2/smoke-step2-batch.log` (City decks Gods and Creature Giving included; Prevent
  Damage has no smoke row); saved as `build/scratch/bloodsun/impl3/smoke-step3-batch.log`.

### Implementation notes (step 4)
**Commit:** `git log --grep "Blood Sun step 4:"`.
- **Params** (`CardParams`): `unlimited_land_plays`, `land_play_damage_after_first` (Fastbond), and the CODE SHAPE of
  step 5's two own-death terms, `lose_life_on_resolve` (0 = none) and `set_all_life_totals` (-1 = absent): parsed, read
  today only by the own-death arming; their cards and consumers are step 5's.
- **Representation (§6):** `kUnlimitedDrops = 64` (`CardDatabase.h`) is folded AT LOAD into `static_extra_land_drops`
  (`unlimited_land_plays` adds it per copy). So the untap seed (both worlds), the mid-turn entry (`FireEtbWatchers`), the
  deck stamp `deck_has_static_land_drops`, the site-10 extra-drop route and arming, the Maze's End provider readers and all
  `LandDropsAvailable()` readers see Fastbond unchanged. Audit of the readers that compute a COUNT from the drops: the only
  ones that would over-credit at 64 are the Gods / Eldrazi Flicker / Snow / AA certificate bounds (upper bounds: safe, and
  none of those decks holds a Fastbond).
- **Damage:** `ResolveLandPlayDamage` (`SpellEffects.h`, after `StaticExtraLandDrops`), called by every land-PLAY body:
  `PlayLandFromHand`, `PlayLandFromLibraryTop` (after `PlayLandCore`, i.e. after the land's own enter triggers -- we order
  the simultaneous triggers and Fastbond last is never worse) and the graveyard land-play ability
  (`ApplyGraveyardPlayAbility`). Never `EnterLand` / Burgeoning / tutors (ruling 2). One event per copy; armed decks go
  through `dmgev::DealDamageEvent`, unarmed decks `life -=` + `MarkPlayerDealtDamage` + a `lifeloss` play event.
  Deck-gated on `deck_has_static_land_drops`.
- **R1 SETTLED: it triggers.** A Lotus Vale / Scorched Ruins whose replacement fails was still PLAYED: CR 305.1 (playing a
  land is a special action; it happened), CR 305.2 (the play counts against the drops; the engine spends the drop), and
  Fastbond ruling 1 (*"You take damage when you play a land using the 'play a land' action"*) keys on the action, not on
  the land entering. Implemented for free: the damage hangs off the play body, which every Vale path returns through.
  Pinned by `bloodsun_fastbond_vale_into_graveyard_pings.json` and the unit test "fastbond R1".
- **Loop-reader audit:** the two greedy multi-drop loops (`AIEngine::TryPlayLand`, the rollout's `SimulateLandPlay`)
  were `guard < 8` (nine lands); now bounded by `LandDropsAvailable()` (each pass consumes a land card; identical below
  ten drops). The two extra-drop RE-ARM pass caps -- ApplyPlanDirect's deferred loop `top_reset_passes++ <= 4` and the
  executor's no-commit twin `pass < 4` -- no longer bind a drop re-arm when `TurnSolver::DropRearmUnbounded` (the active
  player's drops exceed 64, i.e. a Fastbond is out): each such pass needs a land played by the previous one and another in
  hand, so the lands bound it; caps unchanged for every other deck (no Exploration count reaches 64).
- **No-suicide stop (USER D1):** `LandPlayWouldKill` -- own death live and the next play's land-play damage >= life --
  stops both greedy drops (the beyond-horizon / depth-0 policy). A sound dominance (a loss is the worst outcome); false
  for every deck without own death or a land-play damage source. The SEARCH needs no prune: a line whose Fastbond damage
  kills us is scored as a loss (`OpponentHasLost` requires our life > 0), and the scenarios show it holds lands.
- **Drop recompute on leave:** `OnStaticLandDropSourceLeft` -- on its controller's own turn, a leaving Exploration /
  Fastbond takes its `static_extra_land_drops` back out of `bonus_land_drops_this_turn` (nothing retroactive). Hooked in
  `FireLeavesBattlefieldTriggers` (dies / sacrifice via `OnCreatureDies`, Archdruid's Charm exile, flicker, unearth) and
  `DestroyAllEnchantments`. Worldfire's exile-all (step 5) must call it or reseed. **Shared-card change:** Exploration's
  "losing a copy mid-turn does not remove its drop" disclosure is replaced (its oracle note updated); in Maze's End only
  our own Charm could exile it, never chosen autonomously -- smoke below.
- **Own death (§7):** the `life_cost_live` lambda (`GoldFishRunner.cpp`) gains the three terms
  (`land_play_damage_after_first > 0`, `lose_life_on_resolve > 0`, `set_all_life_totals >= 0`). City of Brass alone still
  does not arm it (unit test strips Fastbond from the list: unarmed).
- **Scenario harness:** a `resume_at` fixture skips the untap step, which is what seeds the static drops; `main.cpp` now
  seeds `bonus_land_drops_this_turn = StaticExtraLandDrops` for such fixtures (no existing fixture has a drop-maker out).
- **NOT built (scope = §12 step 4):** §6's land-batch normal form LB, LB2 and the max-first count axis are in no §12 step.
  The engine's existing site-10 machinery searches every land count (one land per section; the d3 unlimited unit test and
  both budgets of the life-3 scenarios find the exact count), so they are pure work-reduction folds for a later step
  (provider / go-off front) once the Fastbond branching is measured.
- **Viewer:** no new decision (how many lands = one land drop per click, §10.1 class A); `audit_viewer_decisions.py`
  classifies both params as INERT_PARAMS (USER sign-off to collect); `--no-sweep` rc 0. No DECISIONS.md row.
- **Card data:** Fastbond in cards.json; Scryfall snapshot appended (no other entry touched); `audit_card_costs.py` on
  Fastbond rc 0 (match); `audit_card_fields.py` rc 0 (oracle bracket-note advisory only).
- **Measurements (step 4 tree):** `mtg-test` 3814 cases, 3814 pass (10 new in `test_bloodsun_fastbond.cpp`: params/fold,
  4 lands -> 3 damage, 2 Fastbonds -> 2 per play, Exploration + Fastbond -> 1 per play, Burgeoning / EnterLand put -> 0,
  R1, leave recompute, greedy no-suicide stop + past the old nine-land cap, own-death arming, the d3-unlimited life-3
  search). `test/scenarios.sh` 618/618 (8 new `bloodsun_fastbond_*`: four human damage fixtures incl. R1; life 3 ->
  plays exactly three lands and wins at 1 life, and holds lands rather than die, each at budget 0 and d3 b100). Smoke:
  235/235 PASS, **0 moved digests** vs `impl3/smoke-step3-batch.log` (Maze's End's Exploration rows included); own death
  stays unarmed elsewhere (no other decklist holds Fastbond; the three params appear only on Fastbond's entry). Saved as
  `build/scratch/bloodsun/impl4/smoke-step4-batch.log`.

### Implementation notes (step 5)
**Commit:** `git log --grep "Blood Sun step 5:"`.
- **Params** (`CardParams`): `look_top_target_player_exile_reorder` (Machinations), `exile_all_permanents`,
  `exile_all_hands_and_graveyards` (Worldfire); step 4's `lose_life_on_resolve` / `set_all_life_totals` now have their
  cards and consumers. `lose_life_on_resolve` is on the Overlord audited whitelist.
- **Grim Tutor.** `ApplyLoseLifeOnResolve` (SpellEffects.h) right after `PerformTutor` in both worlds: life LOSS
  (no `MarkPlayerDealtDamage`, no prevention); armed decks park a loss through `dmgev::SbaCheckpoint`.
  `CastLoseLifeWouldKill`: with own death live, a cast whose own life loss takes us to 0 is dropped from
  `CollectActions` in every autonomous world -- a SOUND dominance prune (a loss is the worst outcome and the fetched
  card only reaches hand; the LandPlayWouldKill twin); a human is still offered it.
  **Deviation (encoding):** `TutorCastTimeAxis` is NOT a cast-time (CollectActions) fan of one cast Action per class.
  That shape would need a plan-signature term (the autonomous signature keys a tutor cast on its NAME only, so the
  dedup would fold the classes back to one) and would put ~19 actions per Grim Tutor into every rollout / d0 `Solve`
  odometer. Instead the hook opts the tutor out of the resolve-mode INDEX axis (`Plan::tutor_choice`) and into a
  post-dedup PER-CAST NAMED axis (EnumeratePlansWithLand, beside the index axis): every Grim Tutor cast of a base plan
  carries its own `Action::tutor_target`, the cross product over the TutorCandidates names (width
  `BloodSunProvider::TutorSearchWidth` = 64 = complete), the first combination written in place. Sound fold: the
  picks of same-name copies are a MULTISET (both orders give the same hand, and the library after both searches is the
  one keyed shuffle of the same remaining set at the same ordinal -- the U7 copy identity). This closes the
  one-tutor-per-plan gap (A4/C6) for this deck and is what the design asked of the hook; the executor honours
  `entry.tutor_target` unchanged. Beyond the horizon (Solve) the tutor resolves to the provider's front -- the
  sanctioned greedy. Inert for every provider without the hook (byte-identical; smoke below).
- **Dimir Machinations.** `LookKind::ExileThenReorder`; `LookTopExileReorder(state, target, n, source)` (SpellEffects.h,
  both worlds): the TARGET's library (R2: fewer cards = all of them), kept cards back on top in the chosen order, the
  rest to exile; an opponent library emptied by it decks the opponent (the TakeFromTop rule). The candidates are
  `EnumerateTopDispositions`' ordered subsets (16 for three) with **fold M1** in `TopDispositionCandidates` (same NAME
  sequence on top = one outcome). **The disposition pin is the Ponder ORDER pin** (`g_scripted_reorder_choice` /
  `Plan::ponder_choice`): the PonderOrderAxis fan sizes Machinations at full width (sum_k C(m,k) k! - 1 variants, sized
  off the TARGET's library) and never applies a provider's Ponder narrowing, so every existing route that carries
  `ponder_choice` (executor install, lazy put-back layout, plan keys, continuation pins) carries it. It does not read
  the scry pin. One look varied per plan (a second Machinations takes the default), the Ponder precedent -- disclosed.
  Provider default: our library -> ScryKeepOnTop in look order, exile the rest; the opponent's -> its lands on top,
  exile the rest (`g_look_target_opponent`). **Target player:** a CollectActions post-pass appends the opponent copy
  (`Action::player_target = 1`, eval <= 0, LAST -- an order, U3 default) when the opponent library is dealt;
  `AIEngine` cast targeting carries it to `StackEntry::player_target` and asks a human through the target chooser
  (players only, the extra-turn shape). `DeckTouchesOpponentZones` reads the param. The library-reader predicates of
  the put-back axis (`PutBackPredictionUnsafe`, `PbCastLibraryBlind`, the cycle-seed purity test) and
  `BpCanonFrontPays` include it. Transmute: the existing `transmute_cost` precedent (no change).
- **Worldfire.** `ResolveWorldfire` (SpellEffects.h, both worlds) in ruling 1's order: every permanent of both sides to
  exile (tokens cease to exist), then every hand and graveyard (2HG second head's hand too), then each total becomes
  `set_all_life_totals` (the opponent's drop sets `opponent_lost_life_this_turn`). After it,
  `FireLeavesBattlefieldTriggers` per nontoken card that left -- which runs `OnStaticLandDropSourceLeft`, so a leaving
  Exploration / Fastbond takes its drops back (the step-4 note). Emrakul is exiled, never put into a graveyard: no
  shuffle. Pending extra turns and floating mana are untouched; the caller moves Worldfire to the graveyard last
  (ruling 3). U2 default: cast-enumerated normally, ORDERED LAST through the no-gain eval (`kNoGainCastEval`), no
  restriction.
- **Viewer:** `WriteTopDecisionJson` emits `type` "exile_reorder", `away_zone` "exile"; `lookPanelHtml` / `commitLook`
  and `SUBDECISIONS` gain the `exile_reorder` branch; DECISIONS.md rows for the look, the players-only target and Grim
  Tutor's `tutor`; `audit_viewer_decisions.py` maps `look_top_target_player_exile_reorder` -> `exile_reorder` and
  classifies `lose_life_on_resolve` and Worldfire's three params as INERT_PARAMS (USER sign-off to collect).
- **Card data:** cards.json entries for Grim Tutor, Dimir Machinations, Worldfire; Scryfall snapshot appended (no other
  entry touched). `audit_card_costs.py` rc 0 (3/3 match); `audit_card_fields.py` rc 0 (oracle bracket-note advisories
  only); `audit_viewer_decisions.py "decks/Blood Sun/Blood Sun.cod" --no-sweep` rc 0 (expected types now include
  `exile_reorder`).
- **Test change:** step 4's "own death: City of Brass alone does not arm it" test now strips all three armers (Fastbond,
  Grim Tutor, Worldfire) -- with step 5's params either of the other two arms it, as designed.
- **Not done / disclosed:** (1) one Machinations look varied per plan (the Ponder precedent); (2) the named tutor axis
  is on base plans only (a variant of another axis -- scry, breakpoint -- resolves its Grim Tutor to the provider's front
  at resolution, exactly as the index axis behaves); (3) the cross product over four Grim Tutors is multiplicative
  (multisets of 19 names) -- unmeasured; the deck is not in the suite yet. Cost belongs to the Stage 4-bis measurement.
- **Measurements (step 5 tree):** `mtg-test` 3821 cases, 3821 pass (7 new in `test_bloodsun_step5.cpp`: params + arming +
  DeckTouchesOpponentZones; Grim Tutor at 3 life not offered / life loss not damage; executor fetch + 3 life; the named
  axis -- 3 singles and all 6 two-copy multisets, never the index axis; Machinations self -- 16 candidates, 16 distinct
  outcomes under the pin and all 16 reached by the plan menu + rollout apply, M1 = 9 for {A, A, B}; Machinations at the
  opponent moves its next land play (Bolt -> Forest drawn and played) and the 9 folded dispositions are all reached;
  Worldfire in both worlds -- Emrakul exiled, library unshuffled, both totals 1, extra turn kept, Exploration's drop
  gone). `test/scenarios.sh` 624/624 (6 new `bloodsun_grimtutor_*` / `bloodsun_machinations_*`, each at budget 0 and d3
  b100: life 3 -> never cast, life stays 3; life 4 -> fetch the bottom Bolt and kill this turn at 1 life; Machinations
  exiles two Swamps to draw the Bolt for a turn-4 kill). Smoke: 235/235 PASS, **0 moved digests** vs
  `impl4/smoke-step4-batch.log`; saved as `build/scratch/bloodsun/impl5/smoke-step5-batch.log`.

### Implementation notes (step 6)
**Commit:** `git log --grep "Blood Sun step 6:"` (WIP if the handoff note `logs/bloodsun/impl-step6-handoff.md` exists).
- **Param** `tutor_up_to_x` (a modifier on `tutor_to_hand`; Diabolic Revelation carries `tutor_to_hand`,
  `tutor_shuffle_after`, `tutor_up_to_x`), so every generic tutor reader (the AcqResolve re-solve after a to-hand
  tutor, the shuffle / library-reader predicates) sees it as a tutor. The single-target axes skip it: the index axis
  (`Plan::tutor_choice`, both fan sites), the per-cast named axis, and `BloodSunProvider::TutorCastTimeAxis`.
- **Package encoding** (`src/core/RevelationPackages.h`): the cast's `tutor_target` = `"<wave>\x1d" + names` (the
  Tooth and Nail encoded-names precedent; no new Action field), `chosen_x = |S|`. Signature term `#DR` + X + package
  in both plan-signature builders.
- **Resolution** `PerformTutorPackageToHand` (SpellEffects.h): names -> hand (a missing name is skipped), at most X,
  ONE `ShuffleAfterSearch`; called in place of `PerformTutor` by EffectHandler and apply_one (Grim Tutor's life loss
  call stays after it, inert here). Human: the `dragon` multi-pick over every library copy, up to X, package preselected.
- **Generator** (`revpkg`): classes = lands by `behaviour_identity` (by `EffectiveLandIdentity` when F-R2 holds),
  nonlands by name, canonical order by name; per X the RANK-LEX enumeration (`FirstFill` / `NextLex`) over the
  provider's `RevelationClassOrder` -- complete and duplicate-free, tested against `CountMultisets` (closed form).
  F-R1 copies, F-R3 X = |S|, F-R4 X >= 1 (autonomous), F-R2 below.
- **Deviation (F-R2 condition):** the design's "no Blood Sun removal except an exile-all-hands effect in either
  decklist" is implemented as a LIVE, conservative scan of OUR hand / library / graveyard / battlefield / stack
  (`revpkg::CardCanEndOwnEnchantment`; the goldfish opponent never casts). **The Blood Sun list's own Ulamog breaks
  it:** its cast trigger exiles two target permanents and, with no opponent permanent, targets OURS -- Blood Sun
  included -- so while an Ulamog is reachable the twin fold is OFF (Vale and Field stay separate classes: more
  packages, nothing lost). Surfaced to the USER (open question).
- **Waves** (`revwave`, TurnSolver.cpp): a normal enumeration admits wave 0 (ordinals [0, K0 = 4) of every X) and sets
  `pending` when an X has more. FSLineWin's **Revelation wave phase** (after the group waves) re-enumerates wave k
  with waves 0..k admitted and keeps only plans whose highest-wave Revelation cast is wave k (the tranche partition:
  multi-Revelation plans mixing waves are covered), prices each wave with `ChargeDecisionUnits(budget, packages
  emitted)` (inert with no budget), stops at `AnytimeStop`, and walks the wave plans' deferred bp ranks. The enum
  memo (`EnumeratePlansWithLand` / M2) is bypassed while the hand holds a package tutor or a wave is enumerating (a
  memo hit would skip CollectActions and lose `pending`); its globals fingerprint gains a last bit.
- **Generator-stop:** no wave is enumerated, and a wave stops mid-way, once `best.win_turn <= state.turn_number` (a
  this-turn kill already returns before the phase). Counters `revwave::g_phase_packages` / `_round_win_stops`.
- **Ordinary order** (`BloodSunProvider::RevelationClassOrder`, §5.4 as a static role key): lands by net mana (Ruins,
  Lotus, Karoos, duals), drop-makers, Revelation, Time Warp, Blood Sun (none out), Emrakul, other tutors, Ulamog,
  Worldfire last. The "binding resource" refinement of drop-makers vs lands is NOT modelled (order only). The go-off
  front (stage 1/2 packages) is step 8's and will plug in ahead of the rank-lex order.
- **Deviation (human X):** X is a `main_phase` plan variant (one cast per legal X 0..max under HumanPlayActive, the
  Genesis Wave shape), not the `x_value` chooser; then `dragon` at resolution. DECISIONS.md row; audit maps
  `tutor_up_to_x` -> `dragon`.
- **Part 2 -- every host reaches every package** (`git log --grep "Blood Sun step 6: (part 2)"`). Two shared helpers
  (TurnSolver.cpp, before FSLineWin), no per-host copies:
  * `RevWaveTranche(k, budget, more, enumerate)` -- one wave of the HOST'S OWN plan list through the host's own
    enumeration call (its parity guards: FSLineWin's fresh axis + bp-root flag, SolveWithLookahead's root
    CondemnSuppressGuard), tranche filter `revwave::PlanMaxWave == k`, priced `ChargeDecisionUnits(emitted)`.
    Hosts: **FSLineWin** (part 1, now through the helper), **SolveWithLookahead** (the executor's no-verified-win
    fallback and the per-turn leaf; tranches built lazily once and rescored per sub_depth pass, the group-wave shape,
    scored by the group-wave scorer `score_gw` -- hoisted out of the group-wave block and shared; deferred bp ranks
    of the tranche plans walked; generator-stop on `pass_best_win <= state.turn_number`), and
    **EnumerateMainPlansExhaustive** (waves appended after the group tranches, budget-checked like them; a capped
    `!tranches` regime keeps wave 0 and reports `truncated`; no score, so no generator-stop).
  * `RevContWaves(...)` -- the **breakpoint continuations' waves** (a Revelation reached only after a breakpoint:
    Grim Tutor finding it, a draw). The continuation list is derived and cached per state and admits wave 0;
    `Plan::rv_cont_wave = k` + `Plan::rv_cont_at` make ONE occurrence read the wave-k list instead
    (`BpEnumEntryForWave`: the same lookup with waves 0..k admitted to the derivation's enumeration and
    `revwave::t_cont_wave` arming the tranche filter in `BpDeriveContinuationList`'s own frame; `BpEnumBuildKey` folds
    both, so it is its own cache entry and the hit-charge replay is unchanged). The executor indexes the same list
    (`TurnSolver::EnumerateBreakpointPlansAtWave`, AIEngine lockstep). The phase is the ordinary deferred walker over
    the host's base plans (`bp_wave0` cleared: every slot from rank 0) in a new wave mode (`BpWaveWalker(...,
    rv_wave)`: stamps rv_cont_wave/rv_cont_at on every hand-out; EMPTY arm and demoted wave off -- they are wave-0
    lists; DEV_SEQ compound slots ON, which is how the breakpoint NESTED after the wave-k continuation -- the
    Revelation's own resolution -- is branched with the wave-k choice held). Triggered only when one of the host's
    own searched applies read a list with `BpEnumEntry::rv_more` (`revwave::ContPendScope`, per host, keyed on the
    rollout nest), next wave only when a wave-k list held more. Priced one decision unit per hand-out; AnytimeStop;
    generator-stop. Hosts: FSLineWin, SolveWithLookahead.
  * Inertness: every new path is gated on a `tutor_up_to_x` card holding packages past its wave (`pending`,
    `rv_more`); the key fold is skipped at wave 0; so other decks are byte-identical (smoke below).
  * **Not covered (stated):** the m2 hosts (FSLineTail's main-2 loop, ApplySecondMainInSearch) still see wave 0 only.
    Design §10.2: second main is not relevant for this deck (every spell is sorcery-speed setup; nothing is created in
    combat), so a main-2 Revelation never beats the main-1 one here; the card is generic, so a deck that casts it in
    main 2 would need the same `RevWaveTranche` call in those hosts. A wave-k continuation inside a wave-k'
    continuation (two Revelations each after a breakpoint) is reached only through the compound slots' first
    deviation (rv_cont_at is ONE occurrence).
- **Still NOT built:** the case-3 last-wave prune (§5.4, default OFF, U4).
- **Part 2 tests** (`test_bloodsun_revelation.cpp`, 3 new; the fixture: Revelation in hand, 7 mana, opponent at 3, the
  only kills {Bolt} / {Bolt, Mountain} are wave-1 packages): SolveWithLookahead returns the wave-1 package and commits
  a this-turn win; EnumerateMainPlansExhaustive's menu holds it (not truncated) while EnumerateMainPlans' wave 0 does
  not; the wave-0 continuation list lacks it, the wave-1 continuation list (`EnumerateBreakpointPlansAtWave`, the
  executor's lockstep reader) holds it and only wave-1 tranche plans.
- **Measurements (part 2 tree):** `mtg-test` 3829 cases, all pass (3 new; plus `test_bloodsun_step5.cpp` repaired: its
  Worldfire arming check built the deck from the shipped list, which lost Worldfire at 04b85bf21 -- the card is now
  added from the card data). `test/scenarios.sh` 626/626. Smoke 235/235 PASS, **0 moved digests** vs
  `impl6/smoke-step6-batch.log`; saved as `build/scratch/bloodsun/impl6b/smoke-step6b-batch.log`.
- **Finding (pre-existing, not a wave defect; surfaced):** a search-level test of Grim Tutor -> Revelation (cast IN the
  continuation) -> both Bolts (the Revelation's own resolution is a NESTED breakpoint) does not win this turn at an
  unlimited budget EVEN WHEN the package is a wave-0 one (10 Swamps + 2 Mountains, opponent at 6: d1 FullSearchLine
  returns win turn 5; with Revelation in hand from the start the same kill is found at turn 4). So the nested
  continuation after a continuation-cast Revelation is not reached by the existing breakpoint machinery (DEV_SEQ /
  nested slots); that is why the continuation host's test is at the list level. Owed: root-cause it (a nested
  breakpoint inside a continuation, all waves alike) -- it bears on the deck's Grim Tutor / Dimir Machinations ->
  Revelation lines.
- **Rules review (2d):** X is chosen on casting (CR 601.2b; X = 0 legal, offered to a human); the printed "up to X" lets
  the player take any number from 0 to X (a package name no longer in the library is simply not taken); "then
  shuffle" is ONE shuffle after the whole search, and it happens even when nothing is taken. The cards go to hand (no ETB, no reveal).
- **Measurements (step 6 tree):** `mtg-test` 3826 cases, 3826 pass (5 new in `test_bloodsun_revelation.cpp`).
  `test/scenarios.sh` 626/626 (2 new: T2 at budget 0 and d3 b100, both win at round 3 through the wave-0 package
  {Ruins x4, Emrakul} with the ORDINARY order -- Revelation X = 5, four Ruins, Emrakul, its extra turn's attack).
  T2 wall: 0.26 s (budget 0), 0.24 s (d3 b100), whole `mtg --scenario` process; `--scenario` reports no unit count
  (MTG_DUMP_UNITS is a batch-mode file), so units are not recorded. The win is a round-3 win reached in the extra
  turn, so the generator-stop ends the node before any wave is generated. `audit_viewer_decisions.py --no-sweep`
  rc 0 (expected types now include `dragon`); `audit_card_costs.py` rc 0 (match); `audit_card_fields.py` rc 0.
  Smoke: 235/235 PASS, **0 moved digests** vs `impl5/smoke-step5-batch.log` (the shared edits -- the {X} block branch,
  the tutor resolution sites, the signature terms, the enum-memo gate's hand scan -- are all param-gated); saved as
  `build/scratch/bloodsun/impl6/smoke-step6-batch.log`.

### Implementation notes (step 6c)
**Commit:** `git log --grep "Blood Sun step 6c:"`. The USER's B3 / B4 rulings (ledger "Decided by the USER") as
`BloodSunProvider` RESTRICTIONS, restrict / order only, behind ONE lever **`MTG_BLOODSUN_USER_PRUNE`** (heurarm slot
`BLOODSUN_USER_PRUNE`, `EnvOn(..., true)`: DEFAULT ON; `=0` is the unrestricted step-6b search, byte-identical to it,
the A/B arm) plus the arm **`MTG_BLOODSUN_EXPLORATION_ANYLIFE`** (slot `BLOODSUN_EXPLORATION_ANYLIFE`, default OFF:
Exploration a target at any life, still never with a Fastbond on our battlefield). Every hook answers the default in
human play and under `DecisionUnpruned(UnprunedGate::Tutor)`. Adoption stays the USER's.
- **Two new provider hooks** (appended to `DecisionProvider`; defaults inert -> every other deck byte-identical):
  `RevelationPackageRestrict(s, hand_index, class_defs, caps, x_min)` (caps may only fall, x_min only rise; the emitter
  drops cap-0 classes from the ranking -- an identity -- and enumerates the rank-lex multisets over the rest, so the
  restricted space is still complete and duplicate-free across the waves) and `RevelationTwinFoldForced(s)`
  (`revpkg::BuildClasses(..., twin_forced)`: F-R2 under any Blood Sun).
- **(a) Revelation X:** X = 0 is already fold F-R4; the hook raises x_min to 2 unless ANOTHER `tutor_up_to_x` card is in
  hand. So 5 mana: never cast; 6 mana: cast only with a spare Revelation; X >= 2 unrestricted.
- **(b) Targets** -- one predicate, `BloodSunTutorTargetCap(s, controller, def, transmute)` (DecisionProviders.cpp),
  read by the Revelation class caps, Grim Tutor's `TutorCandidates` (which also feeds the per-cast named axis) and the
  transmute's (`tutor_exact_mv >= 0`, the synthesized search): Blood Sun cap 1 while none on our battlefield or in hand;
  Fastbond (`unlimited_land_plays`) cap 1 until one is out or in hand; Exploration (`static_extra_land_drops`, nonland)
  only with no Fastbond on our battlefield and life <= 10 (`kBloodSunExplorationLife`, the parent-chosen threshold);
  the big lands (`etb_replace_sacrifice_untapped_lands` / `etb_sacrifice_lands`, adds >= 3: Lotus Vale, Lotus Field,
  Scorched Ruins), Time Warp, Revelation, Emrakul uncapped; the red-source exception by the USER's two NAMES (Rakdos
  Carnarium, City of Brass) when a Blood Sun is in hand, none on our battlefield and no land of ours on the battlefield
  or in hand adds {R}; transmute: Grim Tutor (a single-target to-hand library tutor) plus Blood Sun while we have none.
  Everything else is cap 0. **Order (under the lever only):** `BloodSunRole` -- the step-6 role key, with Fastbond
  moved to 19 (first among the acceleration; it read 30 before, since Fastbond carries `unlimited_land_plays`, not
  `static_extra_land_drops`), Exploration 20; Grim / transmute candidates are stable-sorted by the same key. A Grim
  Tutor left with no admitted target is not cast (`TutorNeedsCandidate`): it would only pay 3 life and shuffle.
- **(b) twin fold:** under any Blood Sun Lotus Vale == Lotus Field even while our Ulamog (whose cast trigger can
  exile our own Blood Sun) is reachable -- the USER's answer to the step-6 F-R2 question.
- **(c) Land plays** -- `BloodSunProvider::LandPlayOffered` (the existing Maze's End hook: the plan land menu, both
  enumeration paths, and the autonomous Burgeoning put), only while no Blood Sun suppresses land abilities: a big land
  that cannot add {R} (Scorched Ruins) never; Lotus Vale only with a Blood Sun in hand that the other mana cannot cast
  (a `TapForCostDirect` probe on a copy, the `MazePlazaPayable` shape) and the Vale's own three can, with >= 2 untapped
  lands of ours, on OUR turn (a Burgeoning put on the opponent's turn cannot cast Blood Sun this turn: never); Lotus
  Field under the same in-hand / cannot-cast condition, >= 2 lands of ours out, and no Lotus Vale in hand (so "ordered
  after any Vale option" holds trivially: the two are never offered together).
- **Invariance:** every restriction reads the UNION of hand and battlefield (invariant when a land moves between
  them) or the life total, so the land variants of one node agree on the package space. Exception, stated: a land
  variant that changes our life (Overgrown Tomb's 2, a Fastbond ping) can cross the Exploration threshold in that
  variant only; each variant's own enumeration (and its waves) stays self-consistent.
- **Not covered / disclosed:** (1) two Grim Tutors in one plan read one candidate list (the named axis' cross product),
  so both may pick Blood Sun / Fastbond -- a second copy the "once we have one" rule would refuse after the first
  resolves; (2) the beyond-horizon greedy land choice (`GreedyLandChoice`, the d0 runner's `TryPlayLand`) does not read
  `LandPlayOffered` (as for Maze's End), so a playout may still drop a Ruins pre-Blood Sun -- the sanctioned greedy,
  but its leaf values a line the search never plays; (3) the step-6b nested-breakpoint gap (a Revelation cast inside a
  continuation) is untouched and no test depends on it.
- **Tests changed:** `test_bloodsun_revelation.cpp` (generator mechanics on Bolt / basics libraries) and the two
  `bloodsun_grimtutor_life4_fetches_and_kills_*` fixtures run with the lever OFF (they pin card mechanics).
- **New tests:** `test_bloodsun_userprune.cpp` (6 cases, each restriction ON and OFF: Revelation at 5 mana never (both
  arms, F-R4), at 6 mana not with one Revelation / cast with two, X = 2 at 7; the restricted package space holds no
  Burgeoning / Ulamog / Marsh / Grim / Machinations / Rot Farm, at most one Fastbond / Blood Sun, and its size equals
  `CountMultisets` of the restricted caps (11 at X = 2; 7 with a Blood Sun in hand); the forced twin fold with an Ulamog
  reachable; transmute = {Blood Sun, Grim Tutor}, {Grim Tutor} with Blood Sun in hand or out; Grim Tutor's list,
  Exploration at life 8 / not 20 / never with Fastbond out (and the any-life arm), Fastbond before Exploration, Time
  Warp before Emrakul, the red-source exception and its two cancellations, a target-less Grim Tutor not cast; the land
  rules on the plan menu). Scenarios (12 new): `bloodsun_prune_grim_bolt_not_target_*` (Grim Tutor not cast, no win,
  life 4: the lever-off twins are the life4 fixtures) and five ON/OFF `expect_menu` pairs: Ruins rows 0 / 1; Vale for
  the Blood Sun 15 / 15; Vale with a Mountain already casting it 0 / 44 (menu 8 vs 52 rows); Field with a Vale in hand
  0 / 2; one other land 0 / 0 (the engine's step-2 folds already hold both Lotuses back there -- the USER rule is
  redundant on that board, pinned).
- **Measurements (step 6c tree):** `mtg-test` 3835 cases, all pass. `test/scenarios.sh` 638/638. Smoke 235/235 PASS,
  **0 moved digests** vs `impl6b/smoke-step6b-batch.log`; saved as `build/scratch/bloodsun/impl6c/smoke-step6c-batch.log`.

### Implementation notes (step 6d)
**Commits:** `git log --grep "Blood Sun step 6d"`.
- **Root cause (item 1).** `ApplyPlanDirect`'s deferred re-solve loop (TurnSolver.cpp, the `while (deferred_cantrip_resolve
  && (top_first_pass || deferred_top_rearm || deferred_drop_rearm || partition_loop) ...)` loop) runs ONE pass unless a
  tutor-to-TOP or an extra-drop re-arm opens another. A continuation's own tutor-to-hand cast (Grim Tutor's continuation
  casting Diabolic Revelation) re-armed `deferred_cantrip_resolve` in the SCORING world (no sink is ever pushed there) and
  that re-arm was simply discarded when the loop exited; in the RECORDED world the arming site's `sink_stack.empty()`
  test failed (the loop pushes `out_breakpoint`), so nothing armed at all. Either way the Revelation's package was never
  re-decided this phase: no host, flag, depth or budget could reach "then cast the Bolts" -- the step-6b finding. Not
  the walker, not DEV_SEQ, not `rv_cont_*`, not a memo key: the occurrence never EXISTED (bp_searched_plan was never
  called for it), so the walker had no slot to open.
- **Scope of the gap.** Every cast-armed class re-arming inside the loop's continuation was discarded in scoring. The
  fix closes it for the ACQUISITION family (the classes whose arming carried `sink_stack.empty()` and that have no
  recorded-world nested resolution): `tutor_to_hand` (spell and ETB), the creature-tutor hook, Garth's acquisition,
  Soulfire's dig. Plain cantrips / `cast_draw` keep their rule: their recorded world resolves a nested draw INLINE
  (`cantrip_breakpoint`, site 0), a pre-existing scoring/recording asymmetry this step does not touch (surfaced).
- **The fix** (TurnSolver.cpp, `ApplyPlanDirect`, the state beside `deferred_drop_rearm`): `cont_apply_depth` counts the
  active `apply_continuation_{precasts,plan,activations}` lambdas, the loop marks its own continuation
  (`in_deferred_cont`, `deferred_cont_base`), and `deferred_cont_top()` is "the top of the loop's continuation" -- the
  same test in both worlds, since it reads no sink. The acquisition arming sites test `acq_arm_ok()` (`sink_stack.empty()
  || deferred_cont_top()`) and call `note_acq_nest_rearm()`, which sets `deferred_nest_rearm` (one more pass; the loop
  condition gains it) and numbers the pass at site 3. An INLINE continuation nested inside it (depth + 2) keeps the old
  rule, so the two worlds stay as symmetric as they were. The new pass is an ordinary class-on occurrence: the
  wave walker numbers it (bp_seen), opens a slot for it, and an untargeted visit takes its canon front (rank 0). The
  committed-line executor replays it as one more recorded segment (the end-of-main catch-all already loops over
  every `rec_bp_ord`); the uncommitted executor re-solves at the first breakpoint with its own search.
- **Second probe (not a Revelation):** Dimir Machinations' transmute (MV 3) can find only Grim Tutor; Grim, cast in the
  transmute's continuation, fetches the Bolt (opponent at 3). Step-6c binary: T5; now T4. So the gap was not
  tutor-to-hand-after-tutor specific: any acquisition cast in the loop's continuation, after any deferred breakpoint.
- **Not reached (surfaced):** the THREE-level chain Machinations -> Grim -> Revelation X = 2 -> Bolts (15 lands) still
  wins T5, also with `MTG_BP_DEV_SEQ_ALL=1`: it needs TWO simultaneous deviations (Grim's target at occurrence 0 is
  rank 2 of [Swamp, Bolt, Revelation]; the X = 2 {Bolt, Bolt} package at occurrence 1 is rank 3). That is the wave
  walker's single-deviation shape (`Plan::bp_at`, L*W not W^L), not the nesting: each occurrence now exists and is
  searched. Step 8's go-off front (a kill package ranked first) makes the occurrence-1 canon the kill, which closes
  this chain without a cross product.
- **Lever:** `MTG_BP_NEST_REARM` (DEFAULT ON; `=0` restores the step-6c one-pass loop byte-identically -- both the
  re-arm and the recorded-world arming are gated) and the diagnostic `MTG_BP_NEST_TRACE` (one stderr line per nested
  re-arm: turn, armer, recorded world, rollout nest).
- **Item 2 (the playout's land choice):** `DecisionProvider::GreedyLandHonoursOffer()` (default false;
  `BloodSunProvider` true). `GreedyLandRefused(state, def)` (LandPlay.cpp; never in human play or under the unpruned
  gate) is consulted by `GreedyLandRank` (GreedyLandChoice / GreedyLandChoiceIndex: the d0 runner's TryPlayLand and the
  executor's greedy drop), `ActivationColorLandIndex`, and the rollout's `SimulateLandPlayOne` (hand walk and the
  Courser top). So a Blood Sun playout no longer drops a Scorched Ruins (or an unadmitted Lotus) pre-Blood Sun; with the
  lever off `LandPlayOffered` answers true and nothing changes. Maze's End (whose Plaza rule rides the same hook) does
  not opt in: its playout is unchanged.
- **Item 3 (USER rulings 80f27768b / 1dd8e3a06, verbatim in the ledger), under `MTG_BLOODSUN_USER_PRUNE`:**
  (a) a Burgeoning put (the opponent's turn) judges Lotus Vale and Lotus Field by ONE rule for our next turn: Blood Sun
  in hand, >= 2 other lands of ours, our other mana (every permanent untapped) cannot cast it; the Field's
  not-with-a-Vale prune applies to our land PLAY only. (b) the red-source exception's two names cap at 1 copy each and
  share a joint cap of 1 per package (new hook `DecisionProvider::RevelationPackageGroups`; the emitter skips a multiset
  over a group's cap without spending a wave ordinal, so the restricted space is still enumerated once; Grim Tutor
  takes one card anyway). (c) either Lotus (play or put) additionally requires that NO non-sacrificing land in hand,
  played / put instead, would make Blood Sun castable (this turn / our next turn): `BsSunCastableWith` /
  `BsNonSacLandCastsSun`, probes on a copy through `PlayLandFromHand` + `TapForCostDirect` (the MazePlazaPayable shape).
  "One class there" is implemented as one RULE (both offered when both qualify), not as a fold of the two into one put
  option -- the put's mechanics differ (Vale needs two UNTAPPED lands at the put).
- **Tests (items 1-2):** `mtg-test` 3838/3838 at item 1 (3 new, `test_bloodsun_nestedbp.cpp`), item 2's prune cases
  7/7 (1 new); `test/scenarios.sh` 642/642 (4 new `bloodsun_nested_*`, all T5 on the step-6c binary, T4 now).
- **Smoke (items 1+2 tree, 2a45f6892):** 227/235 PASS vs GT; vs `impl6c/smoke-step6c-batch.log` **8 jobs moved digest,
  every one with identical played / avg, and 0 per-game win-turn changes** (17 games): edf_d5 gi 57, 71; hinata2hg_d5
  gi 5; hinata_d3 gi 5, 13; hinata_d5 gi 19, 34, 42, 56, 57, 58, 75, 79; mazesend_d5 gi 16; shadowborn2hg_d3 /
  shadowborn_d3 / shadowborn_d5 gi 14. Each deck holds a tutor-to-hand cast that can follow a breakpoint (Hinata's
  Gamble, Maze's End's Archdruid's Charm, ...): the expected shape of this fix, NOT yet confirmed game by game (see the
  handoff, `logs/bloodsun/impl-step6d-handoff.md`). Saved as `build/scratch/bloodsun/impl6d/smoke-step6d-batch.log`.
- **Item 3: DONE in step 6e** (`git log --grep "Blood Sun step 6e"`): the failing red-source case was a FIXTURE error, not
  a code one (see the step-6e notes).

### Implementation notes (step 6e)
**Commits:** `git log --grep "Blood Sun step 6e"`.
- **Item 1 -- the 17 smoke games step 6d moved, root-caused game by game** (table:
  `build/scratch/bloodsun/impl6e/moves.md`). Each game re-run as a one-game batch job off its own smoke manifest row (deck,
  profile, depth, budget, 2HG life / heads, ignore_play_profile; `seed = 1001 + gi`, `game_index = gi`) under
  `MTG_BP_NEST_REARM=1` / `=0`, then through the CLI with `MTG_BP_NEST_TRACE=1` and game logs. **17 / 17: REARM=0 gives the
  GT (6c) digest, REARM=1 the 6d digest**, every win turn equal, every ON run traces nested ACQUISITION re-arms (Gamble,
  Soulfire Eruption's dig, Eladamri's Call, Living Wish, Sylvan Scrying, Diabolic Intent / Demonic Tutor). The moves
  are tie-equivalent lines the search now sees with one more occurrence: a different cast order or main-1 / combat
  split (Hinata x 11), a Ponder shuffle vs keep (hinata_d5 gi 34, T1, through the d5 look-ahead's T3 Gamble re-arms),
  Living Wish -> Adarkar Wastes instead of Dimensional Infiltrator (edf gi 71), a fetched Cloud of Faeries cast after
  the combo win (edf gi 57), two land plays swapped (mazesend gi 16, T2, look-ahead), a main-2 Dark Ritual
  (Shadowborn x 3). Every line is a logged, rules-enforced executor line; none is a bug.
- **Item 2 (6d item 3) -- the failing red-source case was the FIXTURE.** 7 Swamps and ONE Revelation in hand give X = 2
  exactly (X = 1 needs a spare Revelation, B3 (a)), so a package holds two cards; the library's only admitted cards
  were the two red sources, which the joint cap of 1 forbids together, so the restricted space was rightly EMPTY. The
  fixture now carries a Time Warp (`{Carnarium, Time Warp}`, `{City of Brass, Time Warp}`: `with_red == 2`).
- **Item 3 -- new USER restrictions** (`BloodSunProvider`, under `MTG_BLOODSUN_USER_PRUNE`; USER quotes in the code):
  * B8: Revelation class cap 1 per package (`RevelationPackageRestrict`); a package whose every card is a Revelation is
    refused (new hook `DecisionProvider::RevelationPackageRefused`, default false; the emitter skips it like a group-cap
    overflow, no wave ordinal spent, so the restricted space is still enumerated once). X = 0: fold F-R4, every arm.
  * B5: the opponent-target Machinations cast is not offered (new hook `DecisionProvider::OffersOpponentLookTarget`,
    default true, read where TurnSolver appends the opponent variant). The SELF-target cast too while the new
    sub-lever **`MTG_BLOODSUN_PRUNE_MACHINATIONS_CAST`** (heurarm `BLOODSUN_PRUNE_MACHINATIONS_CAST`, `EnvOn(..., true)`,
    DEFAULT ON, PROVISIONAL -- the Stage 5 A/B) is on: `BloodSunProvider::NeverCast` (the hand-cast choke point only;
    the transmute is a Channel action from its own emitter and is untouched -- tested).
  * B10 verified (already B3's rule): a Blood Sun in hand, including one a tutor fetched earlier this turn, leaves Grim
    Tutor the transmute's only target and drops Blood Sun from Grim Tutor's list. New test fetches it with
    `PerformTutor` first.
  * Tests: `test_bloodsun_userprune.cpp` +3 cases (B8, B5 with all three lever arms, B10); the two step-5 Machinations
    mechanics cases run with the prune off; the two `bloodsun_machinations_self_exiles_to_bolt_*` scenarios run with the
    sub-lever at 0 (they pin the self-target mechanics); new `bloodsun_prune_machinations_cast_{on,selfonly,off}` menu
    scenarios (Machinations rows 0 / 16 / 32, opponent rows 0 / 0 / 16).

#### Two-deviation gap (step 6e diagnosis)
**The chain.** Machinations -> (transmute) Grim Tutor -> Diabolic Revelation (Grim's target, occurrence 0, rank 2 of
[Swamp, Bolt, Revelation]) -> X = 2 {Bolt, Bolt} (occurrence 1, rank 3) -> kill, 15 lands, opponent at 6
(`test/scenarios/bloodsun_nested3_mach_grim_rev_bolts_devseqall_*`). Shipped settings: T5 at d1 b0, d3 b0, d8 b0 and
d3 b100.
**Which mechanism, and is the bound deliberate?** Breakpoint-occurrence choices are enumerated by the wave walker
(`BpWaveWalker`): `Plan::bp_choice` / `bp_at` carry ONE deviation per apply, every other class-on occurrence plays its
canon front. `MTG_BP_DEV_SEQ` (default ON) adds deviation SEQUENCES (`Plan::bp_pre`, compound slots, wave = deviation
count, priced through `CanonWaveBillScope`), but its SHIPPED SCOPE opens them only at LATENT occurrences (site 9's
re-open) and canon-drop ones; at every other counted occurrence "the one-deviation walk is the designed coverage"
(TurnSolver.cpp, `BpDevSeqOn`'s comment; `docs/design/bp-empty-arm-deferred-site.md` §6.1). The full cross product is
`MTG_BP_DEV_SEQ_ALL` (default OFF), kept off after the 2026-10-29 measurement (Fluctuator's unbudgeted backstop abandoned
2 of 20 games at the 2,000,000-unit ceiling, Goblins +35-39% units for -0.0007 avg); §6.1 states the gap at counted
occurrences "stays open until" a breakpoint-level transposition (a `BpPrefixSnap` at every site) exists. So the
single-deviation shape is a DELIBERATE, documented bound -- and it is the same open conflict with the completeness
invariant that §6.1 records, not a new defect. Step 8's kill-first package order would make the occurrence-1 canon
the kill: a budget ordering, NOT a completeness fix.
**A real defect found under it: `_ALL` was inert in every FullSearchLine host.** `MTG_DEV_SEQ_STATS=1` with `_ALL=1`:
`pass1=2 met_latent=0 pass2=0`, and `MTG_DEV_SEQ_VERIFY=1`: `TWO-PASS LATER two=5 one=4 UNLIMITED`. FullSearchLine's two
passes (pass 1 with compound waves suppressed, pass 2 only "if pass 1 met a latent occurrence -- the only thing a
compound wave is ever built from") were argued for the shipped scope; under `_ALL` a compound slot is built from any
counted occurrence a variant reaches past its deviation, which pass 1 never recorded, so pass 2 never ran and `_ALL`'s
compound lines were unreachable at every budget (the 6d note "also with MTG_BP_DEV_SEQ_ALL=1" was this, not the walker).
**FIXED (small, sound, inert by default):** at the occurrence-count site of `ApplyPlanDirect`, under `_ALL` and pass-1
suppression, a searched apply's counted occurrence past the plan's deviation is recorded as a (hard) latent one, so the
node is dirty and pass 2 runs. With `_ALL=1` the chain wins **T4 at d1 b0, d3 b0, d8 b0 and d3 b100**; units d3 b0
208 + 51 (pass 2), d3 b100 912 + 137, wall 0.2-0.3 s per scenario process; `_ALL=0` is untouched (smoke below).
**Cost of a complete multi-deviation enumeration.** W^L per main phase over L counted occurrences of widths W (vs the
walker's sum of W), priced per compound hand-out by `CanonWaveBillScope` (inert at an unlimited root, where only the
two-pass branch and bound limits it). The 2026-10-29 `_ALL` cost figures date from the day the two passes landed and
must be treated as unknown for the fixed arm: since the two passes, `_ALL` acted only at nodes that also met a latent
occurrence.
**Proposal (the parent schedules; shared search):**
1. Re-measure `_ALL` with the fix: the unbudgeted backstop rows (Fluctuator d3 / d5, seeds 30009 / 30018, the old
   blow-up), Goblins at its play setting, and Blood Sun held-out once it plays (step 8+).
2. A scoped default-ON candidate that stays complete where it matters here: compound slots at DEFERRED-SITE (site 3:
   tutor / cantrip resolution) occurrences, where `ApplyPlanDirect` already captures a `BpPrefixSnap` (the
   prefix-resume `bp_capture`) -- the transposition §6.1 says `_ALL` needs exists at exactly these sites -- or,
   narrower, at an occurrence CREATED by the deviation itself (the step-6d nested acquisition pass: it exists only
   because the deviating choice fetched a tutor / Revelation, so the one-deviation walk can never vary it -- latent
   in spirit). Waves = deviation count, so a finite budget first spends exactly what it spends today.
3. Keep step 8's go-off front as the budget ORDER on top; it does not substitute for 1-2.
- **Measurements (step 6e tree, 3db695809):** `mtg-test` 3843/3843; `test/scenarios.sh` 647/647 (5 new). Smoke
  (`MTG_PGO=0`): 227/235 PASS vs GT, the same 8 jobs step 6d moved (GT not accepted); vs
  `impl6d/smoke-step6d-batch.log` **0 of 235 jobs moved digest** (B5 / B8 are Blood Sun-only, the `_ALL` fix is inert
  with the lever off). Saved as `build/scratch/bloodsun/impl6e/smoke-step6e-batch.log`.
