# Vampires Stage 2 — cluster A1: MADNESS (Falkenrath Gorger, Markov Baron, Asylum Visitor)

Research design, 2026-10-18, against `.claude/worktrees/vampires` @ 2176032dd. Read-only research:
nothing in the repo was edited. Line numbers are hints (anchor on the symbol); `~` = approximate.
Scryfall JSON: `build/scratch/vampires/scry/*.json`; rulings fetched to `build/scratch/vampires/a1rulings/`.

---------------------------------------------------------------------------------------------------
## 0. Engine facts this design rests on (verified in code)

1. **No madness anywhere.** No `madness` param, no `Keyword::Madness` (`KeywordFromString`,
   `src/cards/CardDatabase.cpp:493-541`, THROWS on an unmapped keyword, so pasting Scryfall's
   `"Madness"` keyword crashes card load until the enumerator exists).
2. **Activated abilities do not use a stack.** Every outlet `Action` (ActivatePump,
   ActivatePermAbility, SacCreatureOutlet, …) is applied synchronously by a shared apply, called
   from the rollout's trailing apply pass in `ApplyPlanDirect` (`TurnSolver.cpp:43918`, e.g. the
   ActivatePump branch ~48708) and from the executor twin in `AIEngine::TakeTurn` (~`AIEngine.cpp:6170`).
   Spells use `state.stack` only in the executor (`GameEngine::ResolveStack`,
   `EffectHandler::Resolve`). **So stack order must be written into the shared apply as explicit
   sequencing** (§2.5).
3. **Mid-resolution mana payment is a known hazard.** `PayOptionalTriggerCost` / `g_etb_optional_payer`
   (`SpellEffects.h:~5280-5320`) documents that no core resolution effect may tap on its own,
   because AIEngine carries an `available` pool across a plan's payments. The safe idiom is a
   **caller-owned payer**: rollout = `TapForCostDirect` (`TurnSolver.cpp:41305`), executor =
   `AIEngine::TapForCost(state, cost, avail, for_creature)` (`AIEngine.cpp:7984`). Both wrap
   `TapForCostShared`; `for_creature=true` is what unlocks creature-spell-only coloured mana
   (Unclaimed Territory, Secluded Courtyard, Cavern; Voldaren Estate per A2) — 12 of this deck's
   20 lands are restricted, so the context matters.
4. **Convoke exists only for Chord of Calling**, inside Chord's own branch of `CollectActions`
   (`TurnSolver.cpp:25643`, branch ~26271-26400). It is GREEN-specific (`ConvokeClasses::free_green`,
   `ApplyConvokeReduction` reduces `c.green`; `SpellEffects.h:2348-2376`, `14548-14580`), and the
   classifier calls a body "free" only if it cannot attack — it does not know about post-combat
   windows or non-mana `{T}` abilities. There is no convoke path for a creature spell.
5. **Discard sites are open-coded and scattered** (full table §2.4). Correction to the brief:
   `MTG_TRACE=discard` is NOT emitted at every real discard; it lives in the *ranking builder*
   `CleanupDiscardRankingWithOrder` (`SpellEffects.h:556-590`), so it fires for the cleanup and for
   ranking-consulting non-cleanup picks (`ChooseNonCleanupDiscardIndex`) and is silent for retrace,
   cycling, channel, transmute, Gamble, Celes, Land's Edge. The only existing "discard goes
   somewhere else" replacement is Progenitus (`MaybeReplaceGraveyardWithLibraryShuffle`,
   `SpellEffects.h:1851`), applied at the cleanup sites only.
6. **The opponent has no hand unless dealt.** `players[1].hand` is EMPTY (never dealt) for every
   deck where `GoldFishRunner::DeckTouchesOpponentZones` (`GoldFishRunner.cpp:524`) is false — true
   only for `exile_opponent_top_cost`. Vampires is not dealt. When dealt (`OpponentDeck.h:73-115`)
   the opponent holds 7 and never casts. The opponent never becomes the active player; its turn is
   modelled at our turn boundary (`opponentdeck::EndOfTurnDraw`, called from `GameEngine::RunTurnFrom`
   ~240 and `SimulateEndAndStartNextTurn` ~50314).
7. **No upkeep draw precedent.** Upkeep triggers are shared lockstep helpers called from
   `GameEngine::UpkeepStep/UpkeepTail` (`GameEngine.cpp:381-505`) and the rollout's turn boundary
   `SimulateEndAndStartNextTurn` (`TurnSolver.cpp:50153`, upkeep ~50460-50600, draw ~50606).
8. **Provider routing hazard (Stage 4a).** `p.convoke` is part of the MELIRA POD routing signature
   (`DecisionProviders.cpp:~21392-21400`, `DetectDecisionProvider` at 21002). Putting
   `convoke: true` on Markov Baron routes Vampires to `MeliraPodProvider` unless something earlier
   claims it. Also `sac_creature_outlet` (if A2 uses it for Pit Fighter) sets the Goblin signature
   (~21433) and `upkeep_adds_charge` (Aether Vial) sets `vial` (~21533). See §6 / risk 1.
9. **Our life can only kill us when `own_death_live`** (`GoldFishRunner.cpp:1459`,
   `GameState.h:403-408`), currently armed only by the Prevent Damage params. Vampires has two
   mandatory/elective self-life-loss sources (Asylum Visitor, Voldaren Estate's pay-1-life mana).

Rulings that decide the model (Scryfall, fetched):
* Discard to pay a COST → the madness trigger (and the spell it casts) resolves BEFORE the spell/
  ability the discard paid for.
* Discard while a spell/ability is RESOLVING → card goes to exile immediately; the trigger goes on
  the stack after that spell/ability has completely resolved.
* Madness works for any discard (cost, effect, cleanup). A madness discard still counts as a discard.
* Declining → graveyard; no second chance. A madness cast ignores card-type timing.
* Gorger: if it leaves the battlefield after the discard but before the trigger resolves, the card
  can still be cast for the Gorger madness cost. If the card has its own madness too, the player
  chooses which instance exiles it. Gorger never gives itself madness (only while on battlefield).
* Convoke: any untapped creature incl. summoning-sick; a multicoloured creature pays {1} or one of
  its colours; convoke combines with alternative costs (madness).
* Asylum Visitor: intervening-if checked as the upkeep begins AND on resolution — with two
  Visitors on YOUR upkeep, the first draw fills the hand and the second does nothing.

---------------------------------------------------------------------------------------------------
## 1. Cards

### 1.1 Falkenrath Gorger — Tier 3 (madness infrastructure); card itself Tier 2 on top of it

Verbatim Scryfall: `mana_cost "{R}"`, `cmc 1.0`, `type_line "Creature — Vampire Berserker"`,
`power "2"`, `toughness "1"`, `keywords []`, `colors ["R"]`, oracle:
"Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is
equal to its mana cost. (If you discard a card with madness, discard it into exile. When you do,
cast it for its madness cost or put it into your graveyard.)"

| Clause | Model | Tier |
|---|---|---|
| 2/1 Vampire Berserker for {R} | vanilla_creature body | 1 |
| "Each Vampire creature card you own that isn't on the battlefield has madness" | static grant, read at DISCARD time by `MadnessCostsFor` (§2.3): a Gorger WE control on the battlefield grants madness to any discarded card whose definition is a creature with subtype Vampire (incl. another Gorger card; never itself, since the discarded card is not on the battlefield and the grant needs a Gorger ON it) | 3 (new mechanic) |
| "The madness cost is equal to its mana cost" | the granted option's cost = `def->card.m_mana_cost` of the discarded card (front face for a DFC), snapshotted into the pending record at discard time (Gorger leaving later does not revoke it — ruling) | 3 |
| Two Gorgers | the grant is a set membership, not a count: one madness option; equal-cost instances (own madness + granted, e.g. Baron/Visitor) are identity-folded to one option | 3 |
| reminder text (exile → trigger → cast or graveyard) | the shared madness core, §2.3 | 3 |

Proposed `cards.json`:
```json
{
 "name": "Falkenrath Gorger",
 "mana_cost": "{R}",
 "types": ["Creature"],
 "subtypes": ["Vampire", "Berserker"],
 "keywords": [],
 "power": 2,
 "toughness": 1,
 "oracle_text": "Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its mana cost. (If you discard a card with madness, discard it into exile. When you do, cast it for its madness cost or put it into your graveyard.) [grants_madness_creature_subtype: a STATIC grant read at the moment of each discard by MadnessCostsFor -- while a Gorger we control is on the battlefield, every discarded creature card with subtype Vampire has madness for its own printed mana cost (front face). The option is SNAPSHOTTED into the pending madness record, so a Gorger that leaves between the discard and the trigger's resolution (e.g. sacrificed to Falkenrath Pit Fighter AFTER the discard -- costs are paid discard-first) still lets the card be cast (2016-04-08 ruling). A discarded Gorger gets madness only from ANOTHER Gorger on the battlefield. Two Gorgers grant one instance; a card with its own equal-cost madness (Markov Baron, Asylum Visitor) has one option. Every discard site routes through the shared DiscardCardFromHand hook; the cast-or-decline is a SEARCHED decision (outlet Action pin) or the provider's dominance rule at cleanup -- see the madness core.]",
 "template": "vanilla_creature",
 "parameters": {
  "grants_madness_creature_subtype": "Vampire"
 }
}
```

### 1.2 Markov Baron — Tier 3 (madness + convoke for a creature spell); lord Tier 1

Verbatim Scryfall: `mana_cost "{2}{B}"`, `cmc 3.0`, `type_line "Creature — Vampire Noble"`,
`power "2"`, `toughness "2"`, `keywords ["Lifelink", "Madness", "Convoke"]`, `colors ["B"]`, oracle:
"Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell
pays for {1} or one mana of that creature's color.)\nLifelink\nOther Vampires you control get
+1/+1.\nMadness {2}{B} (If you discard this card, discard it into exile. When you do, cast it for
its madness cost or put it into your graveyard.)"

| Clause | Model | Tier |
|---|---|---|
| Convoke (hard cast from hand) | NEW creature-spell convoke branch in `CollectActions` (§2.7): CastFromHand variants with `convoke_green`(=pip count, generalized)/`convoke_other`; pip colour = B; reduction + taps via the generalized shared helpers; recompute sites honour it | 2/3 |
| Convoke (madness cast) | same arms applied to the madness cost inside `ResolvePendingMadness` (convoke applies after the total/alternative cost — ruling) | 3 |
| Lifelink | keyword `Lifelink` → `CreatureHasLifelink` (`SpellEffects.h:3573`) at the combat damage site: our life rises. Faithful as-is; only observable through our life total (Estate pay-life, Visitor loss, own death — see §2.9 arming) | 1 |
| "Other Vampires you control get +1/+1" | `lord_effect`, `subtypes_affected ["Vampire"]`, `power_bonus 1`, `tough_bonus 1`, `lord_excludes_self true` (Knight Exemplar shape; per-instance so two Barons pump each other; tokens with subtype Vampire included) | 1 |
| Madness {2}{B} | `madness_cost "{2}{B}"`; with a Gorger out it is the same cost → one option | 3 |

Proposed `cards.json`:
```json
{
 "name": "Markov Baron",
 "mana_cost": "{2}{B}",
 "types": ["Creature"],
 "subtypes": ["Vampire", "Noble"],
 "keywords": ["Lifelink", "Madness", "Convoke"],
 "power": 2,
 "toughness": 2,
 "oracle_text": "Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} or one mana of that creature's color.)\nLifelink\nOther Vampires you control get +1/+1.\nMadness {2}{B} (If you discard this card, discard it into exile. When you do, cast it for its madness cost or put it into your graveyard.) [All four clauses modelled. LORD: lord_effect, subtypes_affected Vampire, +1/+1, lord_excludes_self (Knight Exemplar shape; Vampire tokens included). LIFELINK: the keyword, read by CreatureHasLifelink at the combat-damage site (our life total is real: Voldaren Estate's pay-1-life and Asylum Visitor's loss draw on it). CONVOKE: the Chord of Calling cost-reduction model GENERALIZED to the spell's coloured pip (a BLACK body pays {B} or {1}, any other body {1}; multicoloured bodies count for each colour they have); summoning sickness irrelevant; searched arms = max FREE bodies, then free + k COSTLY bodies (a would-attack body pre-combat, or one with a live non-mana {T} ability such as Bloodline Keeper's); after combat and at cleanup every untapped body is free. Applies to the hard cast AND to the madness cast (convoke applies after the alternative cost). MADNESS {2}{B}: own madness_cost; with a Falkenrath Gorger out the granted instance has the same cost, so one option. The cast-or-decline is searched (outlet Action pin) or the provider's cleanup dominance rule.]",
 "template": "lord_effect",
 "parameters": {
  "subtypes_affected": ["Vampire"],
  "power_bonus": 1,
  "tough_bonus": 1,
  "lord_excludes_self": true,
  "convoke": true,
  "madness_cost": "{2}{B}"
 }
}
```
**Routing:** `convoke: true` trips the Melira Pod signature (§0.8). Either narrow that term to
`p.convoke && p.tutor_mv_max_is_x` (byte-identical — Chord is the only convoke card) or detect a
VampiresProvider ABOVE `melira_pod` / `goblin` / `vial`. Recommend both (§6).

### 1.3 Asylum Visitor — Tier 2 (new upkeep trigger) + Tier 3 (madness)

Verbatim Scryfall: `mana_cost "{1}{B}"`, `cmc 2.0`, `type_line "Creature — Vampire Wizard"`,
`power "3"`, `toughness "1"`, `keywords ["Madness"]`, `colors ["B"]`, oracle:
"At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and
you lose 1 life.\nMadness {1}{B} (If you discard this card, discard it into exile. When you do, cast
it for its madness cost or put it into your graveyard.)"

| Clause | Model | Tier |
|---|---|---|
| 3/1 Vampire Wizard | vanilla_creature body | 1 |
| "At the beginning of each player's upkeep" — OUR upkeep | shared `PerformUpkeepHellbentDraw(state)` in both worlds, FIRST among our upkeep triggers (before Vial; we order our simultaneous triggers and Visitor-first gives the Vial decision the drawn card) | 2 |
| "if that player has no cards in hand" (intervening-if) | checked once per upkeep on NON-STAGED hand cards (staged cards are in exile; the rollout keeps them in `hand`, the executor does not — the cleanup's `s_staged_exempt` lockstep rule). Re-checked on resolution: the first Visitor's draw fills the hand, so N Visitors on our upkeep = ONE draw + ONE life (2016 ruling) | 2 |
| "you draw a card" | `EnterHand(..., HandEntryReason::DrawStep)` (precedes main; not mid-main new material), `cards_drawn_this_turn += 1`; empty library = the draw-step loss path (executor `player_lost_on_draw`, rollout `return false`) | 2 |
| "and you lose 1 life" | `life -= 1` (loss, not damage: no prevention, not routed through the dmg-events flush) | 2 |
| "each player's upkeep" — the OPPONENT's | shared `PerformOpponentUpkeepHellbentDraw(state)` at the turn boundary, immediately BEFORE `opponentdeck::EndOfTurnDraw` in both worlds (their upkeep precedes their draw). Condition reads `players[1].hand` ONLY when `opponent_library_dealt`; when not dealt the passive opponent's notional hand is its untouched opening 7, so the condition is false (faithful — the `target opponent` collapse class, not a deferral). When it does fire, EACH Visitor resolves (our draw does not refill THEIR hand) | 2 |
| Madness {1}{B} | `madness_cost "{1}{B}"` (= mana cost; with Gorger, one option) | 3 |

Proposed `cards.json`:
```json
{
 "name": "Asylum Visitor",
 "mana_cost": "{1}{B}",
 "types": ["Creature"],
 "subtypes": ["Vampire", "Wizard"],
 "keywords": ["Madness"],
 "power": 3,
 "toughness": 1,
 "oracle_text": "At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 life.\nMadness {1}{B} (If you discard this card, discard it into exile. When you do, cast it for its madness cost or put it into your graveyard.) [Both clauses modelled. UPKEEP TRIGGER (hellbent_upkeep_draw / _life_loss / _each_player): OUR upkeep -- PerformUpkeepHellbentDraw, lockstep in GameEngine::UpkeepStep and SimulateEndAndStartNextTurn, ordered FIRST among our upkeep triggers; the intervening-if is evaluated on NON-STAGED hand cards and re-checked on resolution, so two Visitors give ONE draw (2016-04-08 ruling). Draw = a real CR 121 draw (cards_drawn_this_turn), loss = life loss (not damage). OPPONENT's upkeep -- PerformOpponentUpkeepHellbentDraw at the turn boundary just before the opponent's notional draw; it reads the opponent's real hand only when one was dealt (opponent_library_dealt); otherwise the passive opponent holds its untouched opening seven forever and the condition is false. When it fires, every Visitor resolves (our draw does not fill THEIR hand). MADNESS {1}{B}: own madness_cost; with a Falkenrath Gorger out the granted instance has the same cost, so one option.]",
 "template": "vanilla_creature",
 "parameters": {
  "madness_cost": "{1}{B}",
  "hellbent_upkeep_draw": 1,
  "hellbent_upkeep_life_loss": 1,
  "hellbent_upkeep_each_player": true
 }
}
```

---------------------------------------------------------------------------------------------------
## 2. C++ changes

### 2.1 Keyword tag
* `src/core/Card.h` `enum class Keyword` (33-176): add `Madness` before `KeywordCount_` with the
  inert-tag comment (Jump/Landfall precedent; headroom vs the 64-bit static_assert is fine).
* `src/cards/CardDatabase.cpp` `KeywordFromString` (493-541): `if (s == "Madness") return Keyword::Madness;`.
  (`"Convoke"` and `"Lifelink"` already map.)

### 2.2 CardParams (`src/cards/CardDatabase.h`) + parse (`CardDatabase.cpp::BuildParamsFromJson`)
```cpp
// Madness (CR 702.35): this card's OWN madness cost. std::nullopt = no own madness.
std::optional<ManaCost> madness_cost;
// Falkenrath Gorger: while this permanent is on the battlefield, every creature card its
// controller owns with this subtype that is not on the battlefield has madness for its mana cost.
std::string grants_madness_creature_subtype;
// Asylum Visitor: "at the beginning of [each player's|your] upkeep, if that player has no cards in
// hand, you draw N and lose L life". 0 = off.
int  hellbent_upkeep_draw = 0;
int  hellbent_upkeep_life_loss = 0;
bool hellbent_upkeep_each_player = false;
```
Parse: `madness_cost` exactly like `spectacle_cost` / `evoke_cost`
(`CardDatabase.cpp:791-793`, `1444-1445`: `ManaCostFromString(params["madness_cost"].get<std::string>())`);
the rest with `params.value(...)`. Lord params already exist (`subtypes_affected` parse ~811,
`lord_excludes_self` ~986, `CardDatabase.h:750-761`). `convoke` exists (~1133).

### 2.3 The shared madness core (new section in `src/core/SpellEffects.h`)

All functions shared verbatim by executor, rollout and claude-play `ApplyPlan` (which is
`ApplyPlanDirect`, `TurnSolver.cpp:82298`). No new `GameState` field is required (the pending
queue is a caller-local vector — madness always resolves in the same function that discarded), so
`sizeof(GameState)` (`Dominance.h:470` static_assert) does not move. Optional: a deck-constant
`deck_has_madness` presence gate placed in existing bool padding (DECK CONSTANT class — no fold).

```cpp
struct MadnessPending
{
    Card        card;           // the discarded card (in exile while pending)
    int         owner = 0;
    std::vector<ManaCost> costs;   // DISTINCT madness costs, snapshotted at discard (Gorger may leave)
    std::string cause;          // the discard's source ("Stromkirk Condemned", "cleanup", ...)
};

enum class MadnessWindow : uint8_t { PreCombatMain, PostCombatMain, Cleanup, Resolution };

// Caller-owned payer (the g_etb_optional_payer idiom): rollout passes TapForCostDirect, executor
// passes AIEngine::TapForCost with a FRESH AvailableManaPool. The madness payment is a CREATURE
// SPELL payment: for_creature = true, never inside an ability-only pay context.
struct MadnessPayer { std::function<bool(const ManaCost&, bool /*for_creature*/)> pay; };

// One option = (cost index, convoke taps of the spell's pip colour, other taps). Built by
// MadnessOptionsNow from the CURRENT state, filtered by a payability probe on a copy.
struct MadnessOption { int8_t cost_idx; int8_t conv_pip; int8_t conv_other; ManaCost reduced; };

// Pin: -1 = unpinned (provider default; playout / d0 / human preselect), 0 = decline,
// k >= 1 = cast with costs[k-1] and the given convoke counts.
struct MadnessPin { int8_t choice = -1; int8_t conv_pip = 0; int8_t conv_other = 0; };
```

**`MadnessCostsFor(const GameState&, int owner, const Card& c) -> small vector<ManaCost>`**
1. `def = LookupCached(c)`; none → {}.
2. own: `def->params.madness_cost` → push.
3. granted: if `def->card.IsCreature()`, and some permanent CONTROLLED BY `owner` on the battlefield
   has `grants_madness_creature_subtype` that `def->card` carries (`CardHasSubtype`) → push
   `def->card.m_mana_cost`. (Not `c` itself: `c` is in hand, so a lone discarded Gorger gets
   nothing; a second Gorger on the battlefield grants it.)
4. de-duplicate equal costs (Baron/Visitor + Gorger → one; two Gorgers → one).

**THE HOOK — `DiscardCardFromHand(GameState& s, int controller, int hand_idx,
std::vector<MadnessPending>* mq, const char* cause) -> DiscardOutcome`** (every real discard):
1. `Card c = hand[hand_idx]; hand.erase(...)`.
2. `costs = MadnessCostsFor(s, controller, c)`; if non-empty → `mq->push_back({c, controller, costs, cause})`,
   emit play event "X discarded into exile (madness)", return `MadnessExile`. (Madness's "you MAY
   exile instead" is folded: not exiling and exiling-then-declining both end in the graveyard with
   no observable window in this engine — identity fold, so always exile when able; the decline is
   the real choice.)
3. else `MaybeReplaceGraveyardWithLibraryShuffle` (Progenitus) → `LibraryShuffle`, else graveyard → `Graveyard`.
4. Does NOT touch `cards_cycled_or_discarded_this_turn` (sites keep their own increments, so the
   migration of non-madness decks is byte-identical); does not log (sites keep `LogDiscard`).
5. `mq == nullptr` is allowed only for scratch/sim callers; a real site passing nullptr for a
   madness card is a bug → `TRACE("madness", "UNHOSTED ...")` + treat as decline.

**`MadnessOptionsNow(const GameState&, const MadnessPending&, MadnessWindow) -> vector<MadnessOption>`**
For each distinct cost: if the card's def has `convoke`, the convoke arms of §2.7 for that window
(PreCombat: max-free, then free + k costly for k = 1..needed; PostCombat/Cleanup: max-free only —
every untapped body is free); else one no-convoke option. Each option is kept only if a probe on a
COPY can (a) tap the convoke bodies and (b) `TapForCostShared(copy, reduced, /*for_creature=*/true, ...)`.

**`CastFromMadness(GameState&, int controller, const Card&, const std::string& source)`** — the
cast bookkeeping `EffectHandler::PushFreeCast` performs (`EffectHandler.cpp:1046-1064`:
`++spells_cast_this_turn`, `mv_cast_this_turn += printed MV`, `casts_remaining_this_turn`
handling, `FireOnCastTriggers`, `FireProwess` — both shared in SpellEffects.h at 3963/32171), then
`PutCardOntoBattlefield(state, controller, card, source)` (`SpellEffects.h:9499`, both ETB
cascades — `FireEtbWatchers` carries the enters-with replacements such as Giada's, where Vampire
Socialite's should live too) with the play-event verb "cast via madness" instead of "returned".
*Disclosed simplification* (the World War Hulk chapter-I precedent, `PerformSagaFreeCast`
`SpellEffects.h:~30140`): the spell does not physically traverse the stack, so stack-only cast
machinery (`PushCastTriggers`: cascade/demonstrate) is not run. Provably inert for every card that
can currently be madness-cast (every creature in this deck — none has a cast trigger, cascade,
storm, or a counter-relevant clause). Chosen because it is the ONE path both worlds can call at
every madness site, including the rollout cleanup where `apply_one` does not exist — routing each
world through its own cast twin would make the cleanup site non-lockstep by construction.

**`ResolvePendingMadness(GameState& s, std::vector<MadnessPending>& mq, const MadnessPayer& payer,
MadnessWindow w, MadnessPin pin, const std::string& source) -> int casts`** — for each pending in
FIFO order (one controller; order among our own simultaneous triggers is ours, FIFO is the
deterministic choice):
1. `opts = MadnessOptionsNow(s, p, w)`.
2. choice: `pin.choice >= 0` → map to the option with matching (cost_idx, conv_pip, conv_other)
   (absent → decline: the stranded-premise rule); else provider
   `ResolveProvider(s).MadnessCastDefault(s, controller, *def, opts, w)` (returns option index or -1).
   Pins are consumed by the FIRST pending event of the call (an outlet discards one card).
3. `if (g_play_madness_chooser)` → human override (default = the choice above). Inert in
   search/rollout (RevealLogPause nulls it).
4. cast: `ApplyConvokeTaps(s, controller, conv_pip, conv_other, PipColor)` → `payer.pay(reduced, true)`;
   success → `CastFromMadness`; failure → decline (legal; counted by TRACE "madness unpaid").
5. decline: `MaybeReplaceGraveyardWithLibraryShuffle` else graveyard; event "X put into graveyard (madness declined)".
6. `TRACE("madness", ...)` with `g_real_resolution` gating (the discard trace's convention), plus
   an in-window counter: an unpinned (`choice == -1`) event while `greedywindow::InWindow(0)`
   (`TurnSolver.h:815-860`) is reported as `madness-unbranched` (should be 0 in Stage 5).

Safety net: `DrainPendingMadness` is unnecessary because the queue is local; but every site must
call `ResolvePendingMadness` before it returns (unit test §8 asserts `mq.empty()` at every site).

### 2.4 Every discard site → the hook (migration table)

Real discards (route through `DiscardCardFromHand` + `ResolvePendingMadness` at the right point):

| # | Site | World | Discard kind | Madness trigger point |
|---|---|---|---|---|
| 1 | `GameEngine::CleanupStep` loop (`GameEngine.cpp:924-946`) | executor | cleanup (CR 514.1) | after the loop AND after the 514.2 reset block, before the storage-counter bank (CR 514.3a: priority after 514.2, then a new cleanup step — re-run the hand-size check) |
| 2 | `CleanupDiscardShed` (`SpellEffects.h:~652-740`, push ~698) | rollout (`SimulateEndAndStartNextTurn` ~50235) | cleanup | caller: after the shed and after the 514.2 reset block, same position as #1 |
| 3 | legacy cleanup loop (`TurnSolver.cpp:~50245-50268`, `MTG_LEGACY_SEARCH`) | rollout (legacy) | cleanup | same as #2 |
| 4 | `AIEngine::ChooseDiscard` probe trial (`AIEngine.cpp:~8807-8822`) | executor, dormant (`MTG_DISCARD_NODE=1` default) | trial copy | trial must hook + resolve with the provider rule before `RolloutWinTurnFrom(..., ResumeAt::Cleanup)` |
| 5 | `ApplyActivatePump` mode 1 (`SpellEffects.h:13466-13476`, Burning-Fist) | shared | cost | after the discard, BEFORE the +2/+0 (cost-paid rule) |
| 6 | Neheb combat-damage trigger (`Combat.cpp:259-272`) | shared combat | resolving effect | after the trigger's discards complete |
| 7 | Troyan loot `tap_draw_then_discard` (`SpellEffects.h:21794-21806`) | shared | resolving effect | after the ability completes |
| 8 | `ApplyChannel` (`SpellEffects.h:14763-14776`), `ApplyTransmute` (14797-14810) | shared | cost ("discard this") | before the effect |
| 9 | Celes ETB rummage (`SpellEffects.h:~9200-9255`) | shared | resolving effect | after the rummage completes (draws included) |
| 10 | Gamble random discard in `PerformTutor` (`SpellEffects.cpp:~430-470`) | shared | resolving effect | after the spell completes |
| 11 | retrace land discard: executor `AIEngine.cpp:~4512-4534`, rollout `TurnSolver.cpp:~46040-46055` (+ the retrace chooser in ApplyPlan) | both | cost (lands only) | lands never have madness; route for uniformity, resolve before the spell |
| 12 | cycling: executor `AIEngine.cpp:~7814-7827`; rollout cycle-dig `TurnSolver.cpp:~48150-48158`, `~49640-49650` | both | cost ("discard this") | before the draw |
| 13 | Land's Edge pitch (consumers of `LandsEdgePitchOrder`, `SpellEffects.h:~749`) | both | cost (lands) | uniformity only |
| A2 | Stromkirk Condemned / Falkenrath Pit Fighter / Blood token outlets (new) | shared | cost | §2.5 |

NOT discards — must NOT use the hook: Vial/`DeployCreatureFromHand` puts (`SpellEffects.h:21151`,
`22903`, `TurnSolver.cpp:44492`, `AIEngine.cpp:4106`), suspend (16433), staged expiry
(`TurnSolver.cpp:64630`, `67105` — exile), Lackey put (16001), aura swap (`SpellEffects.cpp:2038`),
bottoming (`AIEngine.cpp:1739`, `2111`), casts (`AIEngine.cpp:8603`, land play `LandPlay.cpp:201`),
sims/scratch (`ManaPayment.cpp:2960`, `TurnSolver.cpp:60570`, `DecisionProviders.cpp:1026` shed-verify
reference — names only, `main.cpp:3721` decision preview, `DecisionProviders.cpp:33821/33853/41062` probes).

For every non-Vampires deck the hook's `MadnessCostsFor` returns {} → byte-identical (one def
lookup + one battlefield scan per discard; gate on `deck_has_madness` if the scan shows in a profile).

### 2.5 The outlet contract for cluster A2 (stack order made explicit)

Every A2 outlet apply is ONE shared function in SpellEffects.h, called identically by the rollout
trailing pass and the AIEngine twin, AFTER the caller paid the outlet's OWN mana cost (the
ActivatePump pattern, `TurnSolver.cpp:~48708` / `AIEngine.cpp:~6170`), and takes a `MadnessPayer`:

```cpp
std::vector<MadnessPending> mq;
DiscardCardFromHand(s, ctl, hi, &mq, src);   // 1. COST: discard (madness snapshot) -- FIRST
/* sacrifice cost, if any */                 // 2. COST: Pit Fighter's "Sacrifice a Vampire" AFTER
                                             //    the discard: CR 601.2h/602.2b lets the player pay
                                             //    costs in any order, and discard-first weakly
                                             //    dominates (a sacrificed Gorger would otherwise
                                             //    revoke the grant before the discard)
ResolvePendingMadness(s, mq, payer, window, MadnessPin{a.madness_choice, ...}, src);
                                             // 3. the madness trigger sits ABOVE the ability on the
                                             //    stack -> resolves first; the cast creature resolves
                                             //    and its ETBs fire, all before...
/* ability effect */                         // 4. ...Condemned's "Vampires you control get +1/+1"
                                             //    (applied to Vampires on the battlefield NOW, so the
                                             //    madness-cast creature is pumped), Pit Fighter's
                                             //    "draw two", the Blood token's "draw a card".
```
Condemned stack example, explicitly: activate (discard Baron with madness → exile) → ability on the
stack → madness trigger on top → resolves: cast Baron ({2}{B} / convoke) → Baron spell on top →
resolves: Baron enters (lord applies) → pump resolves → every Vampire incl. Baron gets +1/+1 EOT.
The engine reproduces this exactly by steps 1→3→4 because `ApplyActivatePump`-style team pumps
read the battlefield at apply time (`SpellEffects.h:~13510`).

Payment contexts: outlet cost = ability context (Condemned/Pit Fighter are creature sources →
`CreatureAbilityPayScope`, Secluded Courtyard colours OK; the Blood token is an artifact source →
restricted lands give {C} only). Madness cost = creature-SPELL context (`for_creature=true`;
Territory/Courtyard/Cavern colours and Voldaren Estate's Vampire-spell mana are all legal).

Action fields A2's outlet Actions must carry (add to `Action`, `TurnSolver.h:~420-560`):
```cpp
int8_t madness_choice     = -1;  // -1 unpinned / 0 decline / k>=1 cast with cost k-1
int8_t madness_conv_pip   = 0;   // convoke taps of the madness spell's pip colour
int8_t madness_conv_other = 0;   // other convoke taps
int8_t madness_mv         = 0;   // the madness part folded into a.cost as GENERIC (see below)
```
Dedicated fields, NOT a reuse of `convoke_green/other`: several pre-pass sites tap
`convoke_green/other` keyed only on the counts (`AIEngine.cpp:4893, 5363, 5897, 6750, 7360`;
`TurnSolver.cpp:45268, 49081`; `DecisionProviders.cpp:14214-14224`) and would double-tap.

**Cost bookkeeping (recommended):** `a.cost = outlet cost + {madness_mv generic}` where
`madness_mv = reduced madness cost MV`. Every generic sum/affordability site in the subset walk then
sees the true total without an audit of each `a.cost` reader (fail-safe direction); the colours of
the madness part are checked exactly by the emission-time copy probe (§3.2). The three PAY sites
must split: pay `a.cost − madness_mv generic` in the outlet's context, then let
`ResolvePendingMadness` pay the recomputed coloured madness cost: `SeqWalkFixedOrder`
(`TurnSolver.cpp:8444`, its ability-action branch), the rollout trailing apply, the AIEngine twin.

### 2.6 Cleanup madness (both worlds)

* Order (CR 514.1 → 514.2 → 514.3a): discard (hook) → the "remove damage / until-EOT" reset →
  `ResolvePendingMadness(..., MadnessWindow::Cleanup, MadnessPin{} /*provider rule*/)` → re-check
  hand size (a new cleanup step; madness cannot raise it) → storage-counter bank → opponent turn
  boundary. Same position in `GameEngine::CleanupStep` and `SimulateEndAndStartNextTurn`.
* Payer: executor `m_ai.TapForCost(...)` via a new `AIEngine::PayMadnessCost` wrapper; rollout
  `TapForCostDirect`. Mana pools empty between steps (CR 500.4): clear `state.floating_mana`
  (and the creature/big-spell reserves) before the cleanup payment in BOTH worlds — check the
  executor's end-of-main clear (`GameEngine.cpp:653`, `FloatLeftoverManaEnabled`) and mirror it.
* Decision: provider dominance rule (§3.4), no plan axis, no state pin.
* Human: `discard` chooser picks the shed (existing), then the new `madness` chooser.

### 2.7 Convoke generalized (Chord stays byte-identical)

* `ClassifyConvokeBodies(s, controller, Color pip = Color::Green, ConvokeWindow w = ConvokeWindow::Auto)`
  (`SpellEffects.h:2353`): `pip` replaces the hard-coded `Color::Green`; `w == PostCombat/Cleanup`
  (or `s.phase == Phase::PostCombatMain`) → every untapped eligible body is FREE; pre-combat a body
  is COSTLY if it would attack (`CanAttackFull && pw > 0`, unchanged) OR it has a live non-mana `{T}`
  ability (new: Bloodline Keeper / Lord of Lineage token ability, not summoning-sick — tapping it
  forfeits a token). The `{T}`-ability term can move Chord's classification on Melira Pod only if
  that deck holds such a creature — run it as a play change (suite + Melira held-out A/B).
* `ApplyConvokeReduction(c, pip_taps, other_taps, Color pip = Color::Green)` (14548): reduce the
  `pip` colour field first, then generic. `ApplyConvokeTaps(..., Color pip = Color::Green)` (14561).
* `ConvokePipColor(const CardDefinition&)` = the single coloured pip colour of the card's mana cost
  (Chord G, Baron B); a multi-colour convoke card would need per-colour counts (none exists).
* New creature-spell branch in `CollectActions` (beside Chord's, ~26271): for `def.params.convoke &&
  def.card.IsCreature()`: emit arm 0 = max free bodies (pip bodies cover the pip, then generic;
  others generic), and arms k = 1..(remaining cost) adding k COSTLY bodies in classifier order; drop
  the no-convoke cast when a free body exists (weakly dominated: a free body's tap has no other use
  against the passive opponent, and saved land mana is at least as flexible — the Chord rationale);
  gate each arm on `c.ManaValue() <= pool_total`. Arms over k (not just Chord's free / free+all)
  because tapping ONE attacker to fit a second spell is a real line — the completeness rule.
* Recompute sites to pass the pip colour: `TurnSolver.cpp:8519` (SeqWalkFixedOrder), `23724`,
  `45510` (apply_one), `AIEngine.cpp:8329` (CastSpellFromHand), plus `26288` (Chord emission, default
  arg keeps it green). Signature: extend `#C<g>/<o>` (`TurnSolver.cpp:54813-54820`) — already
  convoke-gated, so Baron's arms stay distinct in the dedup.
* `DeckUsesSecondMain` already fires on `convoke` (`GoldFishRunner.cpp:86`).

### 2.8 Markov Baron lord — no new code (Knight Exemplar shape; ComputeLordBonus per instance).

### 2.9 Asylum Visitor upkeep (both worlds, shared helpers in SpellEffects.h)

```cpp
inline void PerformUpkeepHellbentDraw(GameState& s);          // our upkeep
inline void PerformOpponentUpkeepHellbentDraw(GameState& s);   // their upkeep (turn boundary)
```
* `PerformUpkeepHellbentDraw`: `n_vis` = our permanents with `hellbent_upkeep_draw > 0`; if
  `n_vis > 0` and our NON-STAGED hand is empty: draw `hellbent_upkeep_draw` (from the first such
  permanent), lose its `hellbent_upkeep_life_loss`; ONE resolution total (the intervening-if fails
  for the rest once the hand holds a card). Executor: top of `GameEngine::UpkeepStep` (after
  `state.step = Step::Upkeep`, before `ProcessSuspendArrivals`/the Vial loop — so `ResumeAt::UpkeepTail`
  resumes never re-run it). Rollout: same relative spot in `SimulateEndAndStartNextTurn` (before
  `ProcessSuspendArrivals` ~50460 and the Vial loop ~50462). Empty library → executor
  `player_lost_on_draw = true`, rollout `return false` (draw-step convention).
* `PerformOpponentUpkeepHellbentDraw`: no-op unless `opponent_library_dealt && players[1].hand.empty()`
  (+ `hellbent_upkeep_each_player`); then EACH Visitor: we draw 1 and lose 1. Called immediately
  before `opponentdeck::EndOfTurnDraw` in `GameEngine::RunTurnFrom` (~240) and
  `SimulateEndAndStartNextTurn` (~50314). (`cards_drawn_this_turn` untouched: it is their turn; ours
  resets at our untap.)
* Clairvoyance: both worlds draw the same library top in the same order, so the search's
  next-turn plans see the upkeep card exactly as the executor will. Projection helpers that peek
  "next turn's draw = library.front()" (e.g. `Combat.cpp:~357` FbNextTurnLethal) under-count by
  one card when the Visitor will fire — projections only (Burn/Dragonstorm-specific helpers), not
  play; note in 6a.
* **Own death (recommend arming):** add a deck-trait term so `own_death_live` is true when the deck
  holds `hellbent_upkeep_life_loss > 0` or A2's pay-life Vampire mana (`GoldFishRunner.cpp:1459`).
  The Visitor's loss is MANDATORY; with 4 Voldaren Estates paying life for coloured mana plus
  repeated hellbent upkeeps a long game can reach 0, and today the model would play on. The cost is
  a life check after each main/rollout turn (already implemented for Prevent Damage).

---------------------------------------------------------------------------------------------------
## 3. Search design

### 3.1 Decision inventory (this cluster)
| Decision | Where it arises | How decided in the window | Beyond horizon / d0 |
|---|---|---|---|
| Madness cast-or-decline (+ which distinct cost, + convoke arm) on an OUTLET discard | A2 outlet Actions, main 1 / main 2 | SEARCHED: `Action::madness_choice` variants, explicit pins only | provider `MadnessCastDefault` |
| Madness cast-or-decline at CLEANUP | cleanup shed | provider DOMINANCE rule (cast iff a payable option exists) — sound one-option prune, argued §3.4 | same rule |
| Madness at other resolution-time discards (Neheb, loots, Gamble, Celes) | other decks only | unreachable today (no madness card there); `madness-unbranched` counter guards it | provider |
| Baron hard-cast convoke arm | CastFromHand variants | SEARCHED arms (max free; +k costly) | same arms, front = max free |
| Which COSTLY bodies within an arm | ApplyConvokeTaps | classifier order (mana-payment exemption) + provider hook `ConvokeCostlyOrder` (open question for the USER) | same |
| Which card an outlet discards | A2 | A2's searched axis; madness raises its stakes (§3.6) | A2's ranking |
| Visitor upkeep draw | mandatory | no decision | — |

### 3.2 Outlet madness axis (branched in the rollout, executed identically by the executor)
* **Emission (in A2's outlet emission inside `CollectActions`):** for every discard-candidate
  variant X of an outlet Action: if `MadnessCostsFor(state, me, X)` is non-empty → emit
  `{madness_choice = 0}` (decline) and one variant per payable `MadnessOption` (`madness_choice = k`,
  conv counts, `madness_mv`), each gated by a COPY PROBE that runs the outlet's own payment
  (ability context), the discard, the convoke taps and the madness payment (creature context) —
  exact under the 12 restricted lands, unlike the flat `pool_total` gate. If nothing is payable,
  emit only the decline variant (it is the only legal outcome — not a prune). No `-1` inside the
  window: every emitted variant carries an explicit pin, so the search — not a default — decides.
* **Dedup/fold keys (core invariant — without these the dedup silently keeps one madness answer):**
  follow `convoke_green`'s footprint: `ActionFoldSig` (`TurnSolver.cpp:13387`, fold ~13431),
  `BpCandFingerprint` (~20028, fold ~20076), the action diff printer (~20250), and the
  `plan_signature` in `EnumeratePlans` (`TurnSolver.cpp:51787`, ~54813: add `#MD<choice>/<p>/<o>`
  gated on `madness_choice >= 0`). `Action::operator==` is defaulted (automatic).
* **Execution:** rollout trailing pass in `ApplyPlanDirect`, executor `AIEngine::TakeTurn`, and
  claude-play `TurnSolver::ApplyPlan` (= ApplyPlanDirect) all call A2's ONE shared outlet apply with
  the Action's `MadnessPin` → identical outcome by construction. Commit-the-line replays the Action
  verbatim (no re-decision at execution). A pin whose option is no longer payable at apply resolves
  as decline (legal; the variant then scores as a duplicate of decline, never as phantom mana).
* **Ordering (provider, may order and restrict — never collapse):** `MadnessOptionOrder` default:
  [cast max-free (cheapest cost), decline, cast +1 costly, +2 costly, …]; Bloodghast-class cards
  (A2's graveyard-landfall return) with a land drop still open and a land in hand put decline first.
* **Width control:** with a Gorger out EVERY creature card is madness-capable, so an outlet's fan is
  (discard candidates) × (1 + payable options). Sound folds already applied: equal costs fold; the
  decline of two same-name cards folds (A2's name dedup); post-combat arms collapse to max-free;
  unpayable → decline only. Add an `g_axis_madness_vars` counter beside `g_axis_etbcounter_vars`
  (`TurnSolver.cpp:~59693`) so Stage 5f can see the fan.

### 3.3 Cleanup: the dominance fold, stated so it can be checked
At cleanup, casting a madness card X for which a payable option exists weakly dominates declining,
for every card in this deck: (a) mana at cleanup has no other use (no end-step activation window
exists in the engine, and the pool empties at the step boundary); (b) convoke taps cost nothing
(bodies untap next turn; no blocks vs the passive opponent); (c) the hand is identical either way
(X left it in both branches) so Asylum Visitor sees the same hand; (d) X on the battlefield is not
summoning-sick next turn, whereas declined X is in the graveyard — the only graveyard-reader is
Bloodghast, whose landfall return yields the same permanent a turn later, summoning-sick, and needs
a land drop; (e) no legend collision (Drana/Mavren are 1-ofs) and no "creature count" downside.
So cleanup madness is a provider one-option prune ("cast if payable, max-free convoke, cheapest
cost"), unit-tested (§8), with the decline still offered to the human in the viewer. The base
provider's default is the same rule; a deck with real graveyard payoffs (flashback/delve/threshold)
must override it — say so in the hook's comment. **If the USER prefers a searched axis here**, the
shape is `Plan::cleanup_madness_mask` → a new `GameState::scripted_cleanup_madness` (FUTURE-
DETERMINING; fold it beside `scripted_discard_choice` in `Dominance.h` Build(); size assert moves),
emitted bp-style only for plans whose apply reaches an over-limit cleanup holding a madness card
(the blind per-plan width was refuted for the discard axis — `DecisionProvider.h:2281-2294`).

### 3.4 Beyond-horizon playout policy (the permitted greedy)
`DecisionProvider::MadnessCastDefault(s, controller, def, opts, window) -> int` (base, overridable):
* no payable option → -1 (decline);
* window Cleanup → option 0 (max-free, cheapest) [§3.3];
* main phase: if the card has a graveyard-landfall return (Bloodghast, A2's param) and a land drop
  is still available with a land in hand → -1 (it comes back free this turn); else option 0
  (max-free convoke, never costly bodies).
Used only at depth ≤ 0, by the d0 runner, as the human chooser's preselect, and at cleanup.

### 3.5 No-greedy compliance
In-window madness events are either explicit Action pins (outlets) or the stated sound dominance
fold (cleanup). The `madness-unbranched` counter (unpinned event while `greedywindow::InWindow(0)`)
must read 0 over the Stage 5 sweep; a non-zero count names a site whose enumeration missed the axis.

### 3.6 How madness changes the discard pick (input for A2 / the USER-owned discard buckets)
* A madness-capable card whose madness cost is payable at the discard is the STRONGEST fodder: the
  card is not lost, the creature lands at instant speed, and the outlet's own effect is free —
  Condemned becomes "flash in a Vampire + team pump", Pit Fighter becomes card advantage
  (discard → cast, sac, draw 2).
* Not payable → a madness card is an ordinary discard, EXCEPT Bloodghast, which is good fodder
  either way (landfall return).
* At cleanup the shed ranking should front-load a payable madness creature over a land (it is cast
  AND the land is kept) — a deck-owned bucket rule for the USER (discard buckets are user-owned).
* Outlet timing within main 1: Condemned's pump affects only Vampires on the battlefield at
  resolution, so it wants to come AFTER the turn's creature casts and Vial puts; a madness-cast
  LORD wants to come BEFORE combat. That is cast-order territory — USER-owned; draft, do not adopt.

---------------------------------------------------------------------------------------------------
## 4. Viewer decisions (2c-ter)

| Card / choice | Bucket | Surface |
|---|---|---|
| Gorger grant | — | no choice (static); its effect surfaces through the madness decision |
| Asylum Visitor trigger | — | mandatory, no choice; play events "Asylum Visitor: drew X, lost 1 life" |
| Baron hard-cast convoke ARM | A | `main_phase` plan variants (the Chord precedent; manifest `"convoke": ("main_phase", truthy)` already exists in `scripts/audit_viewer_decisions.py:182-184`) |
| Baron convoke — WHICH bodies | A (reuse `target` via `g_play_loyalty_chooser`, the Terastodon one-at-a-time board pick) | asked inside `ApplyConvokeTaps` only when the chooser is non-null and the pick is not forced; default = classifier order. Also closes the same latent gap for Chord. Autonomous byte-identical. |
| Madness cast-or-decline (+ cost, + convoke arm) — outlet, cleanup, any discard | **B — new type `madness`** | asked inside `ResolvePendingMadness` at every madness event in REAL resolution |

**Bucket B `madness` — the four sites** (copy the `free_cast` shape; DECISIONS.md row 100):
1. `src/core/GameLogger.h`: `using MadnessChooser = std::function<int(const GameState&, int controller,
   const std::string& source, const Card& card, const std::vector<MadnessOption>& options,
   int heuristic_index)>; extern thread_local MadnessChooser* g_play_madness_chooser;` (define in
   `GameLogger.cpp` beside line 48). Null it in ALL THREE RevealLogPause places: the all-null check
   (~970-981), the saved/restore block (~1033/1091/1117/1147), the c-array block (~1198/1215/1234).
2. Call site: `ResolvePendingMadness` (SpellEffects.h) — shared by every world, so `ApplyPlan`
   (claude-play/viewer) reaches it on outlets and `GameEngine::CleanupStep` on cleanup. Reply
   -1 = decline; i = option i.
3. `src/main.cpp`: `WriteMadnessDecisionJson` (beside `WriteFreeCastDecisionJson`, 3404): `type
   "madness"`, `source` (the discard's cause), `card` {name, number}, `options` [{index, cost text
   e.g. "{2}{B}", convoke_taps n, label}], `heuristic_default` (the provider pick; no predates-default
   issue — no reference holds a madness frame yet), `ai_pick`. Chooser lambda in `RunClaudePlay`
   modelled on the free_cast lambda (5751-5790, `claude_retry_*` label); null it beside 4313.
4. `tools/play/index.html`: dispatch `dec==='madness' ? madnessPanelHtml(d)` beside 4038; a MODAL
   (the card is in exile — no visible zone; the cascade free_cast rationale) showing the card image,
   one button per option (cost + "tap N creatures"), and "Put into graveyard".
Then: add the row to `tools/play/DECISIONS.md`; map `madness_cost` and
`grants_madness_creature_subtype` → `madness` in `audit_viewer_decisions.py`'s manifest.
**Human-play collapse:** under `HumanPlayActive()` emit ONE outlet Action per discard candidate
(madness_choice = -1) instead of the madness variants — the human answers at resolution (the
Magnetic Theft "variants collapse to one representative" precedent). Otherwise the menu would show
the same outlet several times AND ask again at resolution.

---------------------------------------------------------------------------------------------------
## 5. Second-main relevance (2c-bis)
* Madness itself adds no second-main requirement (an outlet can fire in main 1).
* It is already ON for this deck: Baron's `convoke` fires `DeckUsesSecondMain`
  (`GoldFishRunner.cpp:86`), and A2's Pit Fighter ("only if an opponent lost life this turn") is
  structurally main-2-only, so its madness casts happen in main 2 — where the convoke classifier
  must treat every untapped body as free (§2.7) and Vampire Socialite's enter-counter rider is live.
* No new `DeckUsesSecondMain` clause needed for A1.

---------------------------------------------------------------------------------------------------
## 6. Interactions for the integrator
1. **Provider routing (MUST, Stage 4a):** Baron's `convoke` → `melira_pod` (`DecisionProviders.cpp:~21395`);
   Pit Fighter via `sac_creature_outlet` → `goblin` (~21433); Aether Vial → `vial` (~21533). Detect a
   VampiresProvider ABOVE these, OR-ed across several cards (`grants_madness_creature_subtype`
   — Gorger, `madness_cost` — Baron/Visitor, `hellbent_upkeep_draw` — Visitor) so one deckbuilding
   swap cannot lose it; and narrow Melira's term to `p.convoke && p.tutor_mv_max_is_x`
   (byte-identical for Melira Pod). The madness provider hooks (`MadnessCastDefault`,
   `MadnessOptionOrder`, `ConvokeCostlyOrder`) live there.
2. **Vampire Socialite (cluster C):** "each other Vampire you control enters with an additional
   +1/+1 counter as long as an opponent lost life this turn" must sit in the shared enter cascade
   (`FireEtbWatchers`, beside Giada's enters-with replacement, `SpellEffects.h:~7440-7470`) so a
   madness cast (`PutCardOntoBattlefield`), a hard cast, a Vial put and a token all get it. A
   madness-cast SOCIALITE in main 2 (after combat damage) puts +1/+1 counters on every other
   Vampire — a real line through Pit Fighter.
3. **Aether Vial:** puts are not casts and not discards; unrelated to madness. Vial puts do not get
   Condemned's pump if they come after it (ordering).
4. **Bloodghast (A2):** the madness DECLINE is a real line (graveyard → landfall return), which is
   why declining is never pruned on outlets.
5. **Bloodline Keeper (B):** its live `{T}` token ability makes it a COSTLY convoke body; madness
   casts count toward its five-Vampire transform.
6. **Lords via madness:** Legion Lieutenant / Stromkirk Captain / Captivating Vampire / Lord of
   Lineage / Baron can all be madness-cast mid-main-1 with a Gorger out, pumping the attack.
7. **Pit Fighter cost order:** discard BEFORE sacrifice (§2.5), else a sacrificed lone Gorger revokes
   the grant. Mid-combat activation (between first-strike and regular damage, once Drana/Captain
   first strike has dealt damage) has no engine window — an engine-wide limitation for A2 to
   disclose, not a madness clause.
8. **Two Visitors + an instant-speed hand-empty in response** (e.g. Condemned at upkeep) — no upkeep
   activation window exists; disclose (engine limitation; the Visitor clause itself is faithful).
9. **Own death arming** (§2.9) touches A2's Voldaren Estate life payments too.

---------------------------------------------------------------------------------------------------
## 7. Deferrals and disclosures
**Proposed deferrals: NONE.** Every clause of the three cards is implemented at Tier 1-3.
Disclosures (not clause deferrals; for Stage 6a):
* D-A1-1 Madness cast = cast bookkeeping + shared enter cascade, not a physical stack traversal
  (`PushCastTriggers` not run). Provably inert for every madness-capable card in the deck. The Saga
  chapter-I precedent.
* D-A1-2 Visitor's opponent-upkeep half reads a real hand only when the opponent was dealt one;
  otherwise the passive opponent's untouched opening seven makes the condition false.
* D-A1-3 Cleanup madness is a provider dominance fold (§3.3), not a searched axis.
* D-A1-4 Which costly convoke bodies to tap follows the classifier order (mana-payment exemption)
  unless the USER wants the provider hook to own it (open question Q2).
* D-A1-5 Optional madness replacement folded into the decline (identity, §2.3).

---------------------------------------------------------------------------------------------------
## 8. Unit-test plan (`test/unit/test_vampires_madness.cpp`, doctest; the `test_magnetic_theft.cpp` fixture shape)
1. Load: the three cards parse; `Keyword::Madness` set on Baron/Visitor; params read back.
2. `MadnessCostsFor`: Baron alone {2}{B}; Visitor {1}{B}; Pit Fighter without Gorger → {}; with a
   Gorger we control → {R}; a Gorger card in hand + Gorger on battlefield → {R}; lone Gorger
   discarded → {}; Baron + Gorger → ONE option; two Gorgers → one; an OPPONENT-controlled Gorger →
   {}; Aether Vial / a land → {}.
3. Hook: madness card leaves hand, not in graveyard, queued; non-madness → graveyard; a Progenitus
   fixture still shuffles (byte-identity of the migrated path).
4. `ResolvePendingMadness`: pin 0 → graveyard; pin cast → battlefield, `entered_this_turn`,
   `spells_cast_this_turn+1`, lands tapped == cost; unpayable → graveyard; Gorger sacrificed between
   discard and resolution → still castable (snapshot); Unclaimed Territory's colour pays {B} for the
   madness cast but not for a Blood-token outlet cost (contexts).
5. Convoke: Baron hard cast ({2}{B}) with 1 summoning-sick black body + 1 sick white body → arm 0
   reduced cost {1} (black pays {B}, white pays {1}); a non-sick untapped Bloodline Keeper is COSTLY; in main 2
   every untapped body is free; Chord fixture: `ClassifyConvokeBodies(s, c)` default args return the
   pre-change classes (byte-identity).
6. Stack order (with A2's Condemned): discard Baron, cast via madness → Baron on battlefield with
   `temp_power_bonus == 1` from the pump, other Vampires +1 (pump) +1 (lord). Run the same Action
   through the rollout apply (`TurnSolver::ApplyPlan` on a copy) and the executor twin; states equal.
7. Pit Fighter cost order: lone Gorger sacrificed as the Vampire, discarded Visitor still madness-cast.
8. Cleanup: hand 8 incl. Visitor, {1}{B} open → Visitor on battlefield, hand 7, graveyard unchanged;
   nothing payable → graveyard; `CleanupDiscardShed` path vs `GameEngine::CleanupStep` path give
   identical states; floating mana from main 2 is not spent at cleanup.
9. Visitor: our upkeep, empty hand → +1 card, −1 life; two Visitors → still one; staged-only hand
   counts empty in BOTH worlds; non-empty hand → nothing; opponent not dealt → nothing at the
   boundary; dealt with an empty opponent hand → one draw per Visitor; empty library → loss flag.
10. Axis: an outlet with a madness discard candidate emits decline + cast variants with distinct
   plan signatures (no dedup collapse); under human play exactly one representative.
11. Viewer: `WriteMadnessDecisionJson` emits type/options/heuristic_default; chooser nulled under
   RevealLogPause (a search inside a human game never asks).
12. Routing: `DetectDecisionProvider` on the Vampires decklist returns the Vampires provider, and
   Melira Pod still routes to `MeliraPodProvider`.

---------------------------------------------------------------------------------------------------
## 9. Risks (top first)
1. **Lockstep drift across the many sites** (outlet apply in two worlds + ApplyPlan, cleanup in two
   worlds, the dormant probe trial, the split payment in SeqWalkFixedOrder). One shared resolver
   with a caller-owned payer removes most of it; verify with the fd-diverge harness (5a) and extend
   `MTG_DISCARD_SHED_VERIFY` to compare madness outcomes.
2. **Two-context payment under 12 restricted lands.** The flat affordability gates are colour-
   blind for the madness part; a wrong gate either drops a real line (lossy) or emits duplicate
   variants. Mitigation: the copy-probe emission gate; apply-time failure degrades to decline.
3. **Width.** Gorger turns every creature into a madness card: outlets × discard candidates ×
   (decline + arms) × Baron convoke arms. Watch `g_axis_madness_vars` and the 5f branching gate;
   the provider may order/restrict, never collapse.
4. **Provider misroute** (Melira via `convoke`, Goblin via `sac_creature_outlet`, Vial) — silent and
   has happened six times; Stage 4a + the routing unit test.
5. **Dedup collapse** if the madness fields miss any fold/signature site (core-invariant violation
   that looks like "madness never matters"). Follow `convoke_green`'s footprint mechanically.

---------------------------------------------------------------------------------------------------
## 10. Open questions for the USER (work proceeds on the stated default)
* Q1 Cleanup madness: dominance fold "cast if payable" (default, §3.3) vs a searched cleanup axis?
* Q2 Which attacking bodies a convoke arm taps: classifier order (lowest power first; default) vs a
  provider-owned order that keeps attack-trigger bodies (Drana, Mavren Fein) untapped?
* Q3 Arm own death for Vampires (Visitor's mandatory loss + Estate life payments)? Default: arm it.
