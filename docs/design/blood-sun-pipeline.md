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
