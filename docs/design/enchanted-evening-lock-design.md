# Enchanted Evening: the lock cluster design (EE, Opalescence, Calming Verse, Parallax Wave, Force of Vigor)

Status: DESIGN (2026-11-25), branch `ee-analysis`, written against `05e2e0030`. No engine code is changed by this
document. It is the brief for the implementer of the lock cluster and the record of how the deck's lock earns
free-turn credit (`docs/design/opponent-lockdown-free-turns.md`, `src/core/FreeTurns.h`). Line numbers below are
approximate (as of `05e2e0030`); search by function name.

Decklist (60): 4 Enchanted Evening, 3 Calming Verse, 4 Sterling Grove, 4 Utopia Sprawl, 4 Wild Growth, 6 Forest,
4 Temple Garden, 1 Exploration, 4 Windswept Heath, 4 Enchantress's Presence, 2 Plains, 2 Idyllic Tutor,
4 Opalescence, 4 Force of Vigor, 4 Wooded Foothills, **4 Parallax Wave**, 1 Sunpetal Grove, 1 Oblivion Ring.
(The USER's "One Parallax Wave" in ruling 1 means one copy on the battlefield suffices; the list runs four.)

## 0. USER rulings (verbatim; binding)

From the ledger (`docs/design/analysis-Enchanted Evening.md`, 2026-11-25):

1. *"Note that the deck locks down the opponent so, like Worldfire, we will need to take that properly into account.
   In particular Calming Verse with Enchanted Evening out removes everything and temporarily locks their lands.
   Enchanted Evening and Opalescence permanently locks the lands, though we should consider cards they can play prior
   to being locked to see if it is a true lock. One Parallax Wave is also this is considered a full lock because any
   creature can be exiled using the Parallax Wave + Opalescence combo and all permanents except Opalescence are
   creatures."*
2. *"(the lock idea is all about getting free turns in particular)"*
3. *"But with the full combo out here, we can attack with Enchanted Evening and Parallax Wave, ending the game pretty
   quickly."* / *"(while the opponent can do nothing)"*
4. *"It should be one 5/5 and one 4/4."* / *"(only enchanted evening costs 5, parallax wave is 4)"*

Received during this design (2026-11-25):

5. *"Yes, Calming Verse + Enchanted Evening is not a full lock, but if they play no land then it gives free turns.
   Also, when Opalescence drops it becomes a full lock as long as there is not a major gap between them."*
6. *"I think "could they actually have cast a permanent with the lands they had in the gap" probably works, but we'll
   discuss if anything else comes up."*
7. *"The default for Parallax Wave and Opalescence in this deck should be to exile all opponent creatures. Then when
   Enchanted Evening drops all of the opponent permanents are creatures. This means that they should all be exiled.
   Fortunately new lands die immediately so that doesn't need to enter the equation."*

Readings this document takes from them:
* The lock is credited ONLY through the existing free-turn rule (`MTG_FREE_TURNS`, default ON): a locked opponent turn
  does not advance the round. No new credit rule is designed here.
* Verse + EE needs no rule change (ruling 5): the existing `LockHolds` credits an opponent turn that ends with no land
  and no creature. The turn after a Verse in which the opponent replays a land and still holds a nonland is NOT free.
  That is the USER's ruling, not an open question.
* "Cards they can play prior to being locked" (ruling 1) is the test of ruling 6: *could the opponent actually have
  cast a nonland permanent with the lands it had* before the lock completed, counted from the last full wipe (a
  Calming Verse under EE) when there was one. §3.4 specifies it. No further "major gap" threshold exists.
* The Parallax Wave override (ruling 1) stands: with EE + Opalescence + an able Wave, every such permanent can be
  exiled, so the lock is full.
* Ruling 7 makes that exile a DECK DEFAULT, not just a credit test: with Wave + Opalescence on our battlefield this
  deck's provider exiles every opponent creature through the Wave's self-exile loop (§4.4.1, "the sweep"); with EE
  also out that is every opponent permanent. It is a USER-owned deck rule, so it lives in the deck's provider (a
  one-option restriction of the Wave decision at the sweep point), never in the engine. Lands the opponent plays
  afterwards die as 0/0s, so the sweep never needs to touch them.

## 1. Oracle text (Scryfall, fetched 2026-11-25; verbatim)

| card | mana cost | MV | type | oracle |
|---|---|---|---|---|
| Enchanted Evening | {3}{W/U}{W/U} | 5 | Enchantment | All permanents are enchantments in addition to their other types. |
| Opalescence | {2}{W}{W} | 4 | Enchantment | Each other non-Aura enchantment is a creature in addition to its other types and has base power and base toughness each equal to its mana value. |
| Calming Verse | {3}{G} | 4 | Sorcery | Destroy all enchantments you don't control. Then if you control an untapped land, destroy all enchantments you control. |
| Parallax Wave | {2}{W}{W} | 4 | Enchantment | Fading 5 (This enchantment enters with five fade counters on it. At the beginning of your upkeep, remove a fade counter from it. If you can't, sacrifice it.) / Remove a fade counter from this enchantment: Exile target creature. / When this enchantment leaves the battlefield, each player returns to the battlefield all cards they own exiled with it. |
| Force of Vigor | {2}{G}{G} | 4 | Instant | If it's not your turn, you may exile a green card from your hand rather than pay this spell's mana cost. / Destroy up to two target artifacts and/or enchantments. |

Scryfall rulings that bind the model:
* Opalescence (2004-10-04): *"Does not animate itself. But can be animated by another Opalescence."* Two Opalescences
  are both 4/4 creatures.
* Opalescence (2008-08-01): *"A noncreature permanent that turns into a creature can attack, and its {T} abilities can
  be activated, only if its controller has continuously controlled that permanent since the beginning of their most
  recent turn. It doesn't matter how long the permanent has been a creature."*
* Enchanted Evening (2008-05-01): *"This has no effect on those permanents other than letting them interact with things
  that care about enchantments."*
* Calming Verse (2004-10-04): *"It checks if you control an untapped land during resolution."*
* Parallax Wave and Force of Vigor: Scryfall lists no rulings.

Colours (for Force of Vigor's pitch): EE is white AND blue (hybrid), Opalescence / Wave / O-Ring / Idyllic Tutor white,
Sterling Grove green-white, Sprawl / Wild Growth / Exploration / Presence / Verse / FoV green. Lands are colourless and
can never be pitched.

## 2. Engine survey (what exists, what it costs)

### 2.1 Card types, creature-ness and P/T

* A permanent carries its OWN `Card` copy (`Permanent::card`, `src/core/Permanent.h`). Its type, keyword and colour
  masks and `m_power` / `m_toughness` (`std::optional<int>`, null for non-creatures) live in the copy's 64-byte hot
  block (`src/core/Card.h`). `Card::AddType` / `RemoveType` mutate the copy; the definition (`CardDatabase` entry,
  CR 706.2 copiable values) is never touched.
* **The precedent this design reuses is Heliod, Sun-Crowned's layer-4 toggle, `RefreshDevotionCreatures`**
  (`SpellEffects.h` ~14197): it toggles `CardType::Creature` on the permanent's copy and is re-run at the shared
  battlefield-membership chokepoints. Its call sites ARE the chokepoint list: `FireEtbWatchers` (the enter cascade top,
  ~13304), `OnCreatureDies` (~17309), `UnearthedLeftBattlefield` (~20952), `PermanentsLeftBattlefieldTogether` (~20997),
  `EffectHandler.cpp` ~1134 (exile/tuck), `PerformReplenish` (`SpellEffects.cpp` ~1274/1294/1306), loyalty death
  (~18471), Ajani (~17959), the executor turn-start resync (`GameEngine.cpp` ~497) and its rollout twin.
  `RefreshReconfigureCreatures` (Lizard Blades) is a second example of the same shape.
* `is_animated` (Mutavault, Gideon) is an UNTIL-EOT animation that also grants haste (`Permanent::CanAttack`:
  `if (is_animated) return true`). It is the wrong tool for a static animation: Heliod's note says so explicitly.
* Summoning sickness is control duration (`entered_this_turn` / `gained_control_this_turn`, cleared at turn start,
  CR 302.6), so an enchantment controlled since our turn began can attack the turn Opalescence arrives, exactly the
  Scryfall ruling. `CanTap` treats non-creatures as never sick; an animated enchantment that entered this turn cannot pay
  a {T} cost, which no card of this deck has.
* P/T: `Permanent::EffectivePower/Toughness` = `card.m_power/m_toughness` (the copy's BASE, layer 7b) + temp bonuses
  (7c) + +1/+1 / -1/-1 counters (7d). Writing the base on the copy is the faithful layer-7b model.
* Read-site census (`grep -rn` over `src/`, 05e2e0030): `.card.IsCreature()` 751 occurrences, `.card.IsLand()` 419,
  `.card.IsEnchantment()` 9, `is_animated` 301, `EffectivePower()` 152, `card.m_power` 110. Definition reads
  (`d->card.IsCreature()` and siblings) 480, almost all on hand / library cards being cast. A crude scan of
  battlefield loops that read the DEFINITION's type or P/T instead of the copy found 23 sites (DecisionProviders
  4216, 33410, 34439, 42002, 57242, 57772, 58844, 70714; TurnSolver 18798, 40216, 43421, 46322, 47403, 69443, 80478,
  103501, 104730, 105348; ManaPayment 95, 4526, 7218; SpellEffects 43506, 43694). Nearly all are gated on a param this
  deck does not hold (lords, blink, creature-mana, sac-fodder, Spellskite-style shroud bodies). **Conclusion: the
  copy-mutation pattern makes the ~750 copy readers right at once; the implementer must audit the 23 definition
  readers (and any later ones) and record each as "param-gated, unreachable for this deck" or fix it to read the
  copy.** The reads that matter for this deck all read the copy already: attack eligibility (`CanAttack`,
  `CanAttackFull` ~9824), combat damage (`EffectivePower`), both SBA passes (executor
  `GameEngine::CheckStateBasedActions` ~1724 reads `p.card.IsCreature()` + `EffectiveToughness`; the shared
  `SweepZeroToughnessSba` ~20853 the same), free-turn condition 2 (`NoOpposingCreature`, `p.card.IsCreature() ||
  p.is_animated`), and the enchantment-target filters (`SpellEffects.h` 21676 / 21681 / 22278 / 39886, all
  `q.card.IsEnchantment()`).
* **The rollout runs no SBA pass between actions** (`SweepZeroToughnessSba`'s header, MTG_LORD_LEAVE_SBA). Any effect
  that can drop a toughness to 0 must call the shared sweep itself, in both worlds. That is the second precedent this
  design reuses.
* Mana value of a permanent: `card.m_mana_cost.ManaValue()` on the copy (X counts 0 on the battlefield, CR 202.3e);
  tokens and lands 0. A spell // land MDFC on the battlefield showing its land face has MV 0 (the face that is up;
  CR 712.8e's front-face MV rule is for TRANSFORMING DFCs; the implementer should confirm against the CR text in the
  rules skill before relying on it -- it only affects opponent MDFC lands, which die either way at MV 0).
* No existing card SETS base P/T of a noncreature permanent statically. Saproling Burst's tokens (`RefreshFadeTokens`)
  are the closest P/T-refresh precedent.

### 2.2 Mana

* Hybrid: supported (`ManaCost::hybrid_pair`, first colour baked into the flat ints, `ExpandHybrids` adds the second).
  `{W/U}` already appears in cards.json (`{2}{W/U}`). EE is Tier 1 for its cost: this list pays both pips with W.
* Floating mana: `GameState::floating_mana` (`ManaPool`, 626 references), the turn/phase-scoped reserve rituals and
  Lotus Bloom use. `ManaPool::wild` = "one tap of a multi-color land (satisfies any single color or generic)". The
  Worldfire ledger records why Lotus Bloom floats ONE colour, not wild: wild can pay a colour the source cannot make.
* **Precedent for a dominance pre-float: Saproling Burst's doomed-body pre-float** (`MTG_FADE_PREFLOAT_DOOMED`,
  default ON, `SpellEffects.h` ~41870): a token the activation is about to kill is tapped into the float first,
  "the float only ADDS mana, decides no colour (one free face), and changes nothing else"; shared by every apply site,
  so both worlds stay in lockstep.

### 2.3 The opponent

* Opponent permanents: (a) dealt LANDS (`opponentdeck::Deal`, numbered `kNumberBase` 100000 + list index, second head
  `kNumberBase2` 110000; hurdle basics +9000, placeholders +9100), put by `OpponentLandEnters` (`SpellEffects.h`
  ~3403) with a direct `battlefield.push_back` (NOT the enter cascade); (b) scheduled SPAWN bodies
  (`OpponentSpawn{turn, power, toughness}`, materialised as definition-less creature tokens named "P/T Creature" at our
  untap: `GameEngine::UntapStep` ~563 and its rollout twin `TurnSolver.cpp` ~85816, also direct pushes). The opponent
  never casts, never blocks, never attacks. Its nonland hand cards are only ever discarded (cleanup, Sire, Worldfire).
* The opponent's whole turn is simulated at the end of ours: `opponent_turn` lambdas in `GameEngine::RunTurnFrom`
  (~350-385) and `TurnSolver`'s `SimulateEndAndStartNextTurn` (lockstep): draw, `ResolvePassiveOpponentLandTurn`
  (shared; untap, land drop / fetch, 2HG second head), `TryOffMainAltWinActivation`, Sire's check + discard,
  `PerformEndStepSelfSacrifices`, `PerformOpponentEndStepFaceUp` (the end-step window), cleanup. Then
  `freeturn::NoteOpponentTurnEnd`.
* **We CAN act on the opponent's turn, in two precedents:** (1) Archive Trap's opponent-turn window inside
  `ResolvePassiveOpponentLandTurn` (cast for {0} after an opponent fetch); (2) **the opponent END-STEP window**
  `PerformOpponentEndStepFaceUp` (morph flips, Soulstoke puts): a menu (`EotFaceUpMenu`), searched at the turn
  boundary as a `BoundaryFan` digit (`ChooseRealCrossingFaceUp`, `TurnSolver.h` ~3214), pinned as
  `GameState::scripted_eot_face_up` / `Plan::eot_face_up_choice`, consumed in both `opponent_turn` lambdas, human
  prompt `g_play_face_up_chooser`. It runs AFTER the opponent's land drop and BEFORE the lock check. Force of Vigor's
  pitch joins this window (§4.5).
* `PassiveOpponentTurnScope` / `t_passive_opponent_turn` marks "it is not our turn" (active_player_index stays ours
  through the simulated opponent turn).

### 2.4 Counters, exile-until-leaves, destroy templates

* Fading: `CardParams::fading_counters` (Saproling Burst), counters in `Permanent::fade_counters`, put at both enter
  sites (`EffectHandler.cpp` ~154, `TurnSolver.cpp` ~80523), removed at upkeep by the shared `PerformUpkeepFading`
  (~30506), sacrificed on failure to remove (CR 702.32). Remove-a-counter activation: `fade_saproling_cost` /
  `PermAbilityMode::FadeSaproling` (no {T}, no mana, repeatable, legal the turn it lands). Parallax Wave's fading is
  Tier 1; its activation is a new PermAbilityMode beside FadeSaproling.
* Exile-until-leaves: `Permanent::linked_exile_number` (Brutal Cathar: ONE card, returned when the source leaves;
  folded nonzero-gated). The Wave needs a LIST (§4.4).
* Destroy templates: `destroy_all_enchantments` (Reverent Silence, includes ours); `etb_destroy_artifact_enchantment_land`
  (Acidic Slime, one target either side); `cast_trigger_destroy_target_permanents` (Ulamog the Infinite Gyre, N
  distinct targets, searched `Plan::cast_trigger_target_choice`); pitch-evoke alt cost `evoke_exile_color` (MH2
  Incarnations: which card is exiled is a searched axis, `Action::pitch_name` / `pitch_slot`).

### 2.5 Free-turn machinery that this deck touches

* `freeturn::LockHolds`: every head (no land on the battlefield OR no nonland in hand) AND no opposing creature
  (board, plus a scheduled spawn that `SpawnMayAppear`). Read at the END of the opponent's regular turn.
* `RuleArmed` = `MTG_FREE_TURNS` AND `deck_has_free_turn` (= `GoldFishRunner::DeckMayLockOpponent`). A deck the trait
  misses gets NO credit (the FreeTurns.h header is authoritative; the older comment above `DeckMayLockOpponent` that
  says "a deck this misses still receives the credit in play" is stale and should be corrected in the same change).
* The reach certificate (`FreeTurnMayFollow`, `MTG_FREE_TURN_REACH`) applies when `deck_free_turn_hand_intact`
  (`DeckLockNeedsLandStrip`). Its soundness premise is "our removals act on the battlefield, which its land drop
  refills". **That premise is FALSE for this deck twice over** (§3.5).
* Depth counts ROUNDS: a free turn costs no ply. `TurnSeqCapped` ends a game whose turns TAKEN exceed 4 x max_turns
  as a no-win (termination backstop; `free_turns_taken` is a saturating uint8).

## 3. The lock and free turns

### 3.1 What each configuration does under the model of §4

**(A) Calming Verse with EE on the battlefield ("temporary", rulings 1 and 5).** Verse destroys every opponent
permanent (all are enchantments): lands, spawn bodies. At the end of the opponent's next turn: it draws and plays the
leftmost land of its hand if it has one.
* It held no land: no land on the battlefield, so condition 1 holds; spawns need a land (`SpawnMayAppear`), and they
  are scheduled for turns 1-3 only, so condition 2 holds. **`LockHolds` fires: free turn.** This repeats every turn its
  hand stays land-less.
* It replayed a land and holds a nonland (the usual case: the engine opponent never casts, and the hurdles give it two
  nonlands): **NOT locked. Not free** (ruling 5). Unless a Force of Vigor pitched at its end step destroys that land
  (§4.5): then condition 1 holds again and the turn IS free (subject to §3.4).
* Params: Verse's new param + EE's new param (+ FoV's for the end-step route).

**(B) EE + Opalescence ("permanent").** Every land on both sides is a 0/0 creature and dies to SBA (CR 704.5f), and so
does every land that enters later, before any player could tap it (SBAs are checked before a player receives
priority, CR 117.5 / 704.3). Every opponent spawn token (MV 0) dies as it appears. At the end of every later opponent
turn: no land (condition 1), no creature (condition 2; a scheduled spawn cannot appear without a land, and would die).
**`LockHolds` fires every turn: every later turn is free, so the round never advances again** and the game's win turn
is the round in which the lock completed, provided the clock finishes inside `TurnSeqCapped` (EE alone is a 5/5:
four attacks for 20 life, six for 2HG's 30). Subject to the fairness check (§3.4): if the opponent could have deployed
a nonland permanent of MV >= 1 before the lock, it survives as an MV/MV creature and condition 2 fails.

**(C) EE + Opalescence + Parallax Wave ("full").** As (B), and the deck-default sweep (ruling 7, §4.4.1) runs at
every opponent end step before the lock check: it exiles every opponent creature, real (a spawn body) or virtual
(§3.4), so the check sees no opposing creature, no land, and no spawn that can appear. **`LockHolds` fires every turn:
every later opponent turn is free** (an unremovable hexproof virtual creature excepted, §3.4.4).

Two corollaries the implementer and the USER should see:
* Under (B)/(C) the deck's OWN lands are gone too. All mana after the lock comes from what was floated in the phase
  the lock completed (§4.2). The search must sequence the turn so everything castable is cast before or with that
  float.
* The kill is quick in TURNS TAKEN (EE 5/5 + Wave 4/4 = 9 a turn, ruling 3/4), and free in ROUNDS.

### 3.2 Params that join `GoldFishRunner::DeckMayLockOpponent`

New params (names are proposals; the implementer may rename consistently):

| card | param | trait |
|---|---|---|
| Enchanted Evening | `static_all_permanents_enchantments` (bool) | required |
| Opalescence | `static_animate_other_nonaura_enchantments_mv` (bool) | with EE |
| Calming Verse | `destroy_opp_enchantments_then_own_if_untapped_land` (bool) | with EE |
| Force of Vigor | `destroy_up_to_artifacts_enchantments` (int, 2) + `alt_cost_exile_color_off_turn` ("G") | with EE |
| Parallax Wave | `fading_counters` 5 (existing) + `fade_exile_target_creature_cost` (int, 1) + `ltb_returns_exiled_with` (bool) | not needed |

The trait is a COMBINATION: `DeckMayLockOpponent` is true when the deck (main, plus a wishable sideboard as today)
holds an EE-param card AND at least one of the Opalescence / Verse / FoV-destroy params. None of the three touches a
land without EE, and EE alone touches nothing, so neither alone joins (a deck holding only Verse earns no credit and
pays no stand-down). The Wave is not needed: it never strips a land; its role is the fairness override.

### 3.3 `DeckLockNeedsLandStrip` must return FALSE for this deck

The reach certificate claims "a head holding a sure land and a plain nonland ends its coming turn controlling a land
AND holding a nonland, whatever the rest of OUR turn does". Both halves of its premise fail here:
1. Under EE + Opalescence the opponent's land drop dies on entry (a static, not a removal of a battlefield land).
2. A Force of Vigor pitched in the opponent's end-step window destroys the land it just played.
So the certificate would certify turns that then lock (the audit `MTG_FREE_TURN_REACH_AUDIT` would report UNSOUND).
Add the EE param and the FoV off-turn alt-cost param to `DeckLockNeedsLandStrip`'s `touches` list (or, cleaner, return
false when `DeckMayLockOpponent` was satisfied through the EE combination). The cost is the unconditional turn-edge
prune stand-down for this deck (Sea Creatures measured +26% search units for the same stand-down). A tighter
certificate ("no EE+Opalescence reachable by the end of this turn, and no FoV in hand with EE in play") is possible
later; it is a measured refinement, not part of the first build.

### 3.4 The fairness check: "cards they can play prior to being locked" (rulings 1, 5, 6)

`MTG_FREE_TURN_EE_FAIR`, DEFAULT ON (the USER asked for it). A check-and-record in the style of
`NoteSireBeforeDiscard` / `AnnihilatorWouldKeepPermanent`: it never makes the opponent cast anything, never changes its
hand or the real board. It keeps a VIRTUAL BOARD of the permanents a real opponent would have deployed, and vetoes a
lock that board would break.

#### 3.4.1 State

`GameState::opp_virtual_board[2]` (`uint64_t`, one per head): bit `i` set = the head's card numbered `band + i`
(`kNumberBase` / `kNumberBase2`; `i` < 64 covers every 60-card pool list; a list longer than 64 must take a vector, and
the implementer should static-assert or check the pool) is VIRTUALLY ON THE BATTLEFIELD. Hurdle basics (+9000) and
placeholders (+9100) are never recorded (a land, an uncastable sorcery). The name behind a number is recoverable from
the dealt list's spec order (`Pool()[slot].cards` expansion, the numbering `Deal` uses); a helper
`opponentdeck::CardOfNumber(state, number)` does it. The masks are FUTURE-DETERMINING (they decide later lock credit),
so they are folded into BuildSimKey and BoardSignature NONZERO-gated, like `fade_counters`. Zero for every other deck.

#### 3.4.2 Recording (both worlds, lockstep)

`freeturn::NoteVirtualDeployments(state)`, called in both `opponent_turn` lambdas right after
`ResolvePassiveOpponentLandTurn` (after its land drop, i.e. in its main phase) and BEFORE the end-step window
(Sire's check, `PerformOpponentEndStepFaceUp`, the FoV pitch). Gate: `RuleArmed(state) && state.deck_has_ee_lock &&
FreeTurnEeFairOn()` (`deck_has_ee_lock` = a deck constant stamped in `StampDeckTraits` when the deck holds the EE
param). For each head (1, and 2 under 2HG):
* `m = opponentdeck::HeadManaOf(state, head)`: the lands it controls now, after its drop. Under EE + Opalescence its
  drop has already died, so `m.lands == 0` and nothing is recorded. After a Verse it is the land count it has rebuilt.
* Walk its hand LEFT to RIGHT (the convention of its land policy). For each card not already in the mask whose
  definition is a nonland PERMANENT card (`IsPermanentCard`; an MDFC by its front face; the placeholder never), not an
  Aura (an opponent Aura is never a creature under Opalescence and cannot break condition 2), and castable with the
  REMAINING budget (`CastableWith` against `{lands = m.lands - spent, sources = m.sources}`: MV with X = 0, each
  coloured pip covered by a land producing it, hybrid pips by their first colour as `ManaCost` stores them): set its
  bit, `spent += MV`.
* This spends a real mana budget per turn (it cannot deploy two 3-drops off three lands), and colour-checks against
  total sources without decrementing them per pip (the generous side, as the annihilator check is). An unknown card
  (no definition) is never recorded; the implementer should confirm every pool card has a cards.json entry (the
  Sire / annihilator checks rely on the same).

This is exactly ruling 6's test, turn by turn: a card is recorded only if the opponent could actually have cast it
with the lands it had on that turn.

#### 3.4.3 Clearing (what our effects remove from the virtual board)

* **Calming Verse** (on resolution, after its first clause): clear every bit whose card is an enchantment under the
  current statics, i.e. ALL bits while EE is on the battlefield (CR: "Destroy all enchantments you don't control";
  under EE every virtual permanent is an enchantment), only enchantment cards otherwise; a card whose definition has
  `Keyword::Indestructible` stays. **This is the "gap" of ruling 5/6, stated precisely: after a Verse under EE the
  virtual board holds exactly the permanents recorded on opponent turns that ended AFTER the Verse resolved and before
  the lock completed, each one castable with the lands the opponent had rebuilt by then.** Verse and Opalescence in
  the same turn (either order, the second off the float, §4.2) leave no gap at all; Opalescence the very next turn
  leaves one opponent turn on at most one land, so only a castable 1-drop can break it.
* **Parallax Wave + Opalescence, the deck-default sweep** (ruling 7, §4.4.1): clears every bit that is a virtual
  creature at the sweep (§3.4.4's definition) and that it can target (no hexproof). The loop's exiles are permanent,
  so the bits stay cleared even if the Wave later leaves.
* **Force of Vigor and Oblivion Ring are NOT credited against virtual permanents** in the first build (the agent
  default; see USER question Q1). Both target, so crediting them means choosing virtual targets; the conservative
  under-estimate (the Sire precedent) leaves them out.

#### 3.4.4 The veto, and the Parallax Wave override

In `LockHolds`, after the existing vetoes: for each head, each bit `v` of its mask is a VIRTUAL CREATURE at the check
when
* Opalescence is on our battlefield AND `v` is a non-Aura enchantment under the statics (an enchantment card, or any
  card while EE is on the battlefield): a creature iff `MV(v) >= 1` (base P/T = MV; an MV-0 one dies as a 0/0);
* otherwise: iff `v` is a creature card.

The veto: if any bit is a virtual creature at the check, the lock fails (`return false`). With Wave + Opalescence on
our side the sweep (§4.4.1) has already run in this same opponent end step, immediately before the check, and cleared
every virtual creature it could target; what remains is only what the Wave cannot exile: a card with
`Keyword::Hexproof` (the Wave targets; the engine has no Shroud / Protection keywords, so those are not modelled and a
virtual card with them is treated as targetable: disclose), or everything when the Wave is not ABLE (§4.4.1: fewer
than `1 + G` fade counters with `G` of our Sterling Groves out, or `G = 4`).

So, per configuration, with the check ON:
* (A) Verse + EE: the Verse cleared the board; a turn the opponent ends with no land in hand is free (it could not
  cast anything). A turn on which it replayed a land, deployed a creature card with it and lost the land to an
  end-step FoV is vetoed (it really had that creature out). A noncreature permanent does not veto without Opalescence.
* (B) EE + Opalescence: free from the lock round on iff nothing MV >= 1 was recorded since the last Verse wipe (or
  since the game began). Against a typical pool opponent that has had lands for several turns this usually vetoes,
  which is the USER's "see if it is a true lock".
* (C) EE + Opalescence + an able Wave: the sweep clears the board at every opponent end step, so every turn is free
  (hexproof virtual creatures excepted).

### 3.5 2HG (two opposing heads)

The suite has 2HG keys; this deck gets `<deck>2hg` rows like `worldfire2hg`.
* The lock needs BOTH heads (`HeadLocked` for 1 and 2). Under (B)/(C) both heads' lands die, so nothing changes.
  Under (A) both heads must end land-less: rarer, and each head's replayed land needs its own FoV target (one FoV has
  two targets, so one FoV can cover both heads in one end step).
* Calming Verse: "enchantments you don't control" is every opposing permanent of BOTH heads. FoV's two targets may
  split across heads. The Wave targets either head's creatures.
* Fairness: one mask per head, each recorded against that head's own lands (`HeadManaOf(state, head)`) and hand
  (`opponent2_hand` for head 2), cleared and vetoed on the shared board.
* Life 30 (team): EE + Wave take 4 attacks instead of 3; with free turns this changes the turns taken, not the round.
* Force of Vigor's "if it's not your turn": the opposing team's turn is not ours, so the pitch is legal in the 2HG
  end-step window as in 1v1.

## 4. Faithful data model, card by card

General gating rule for every item below: the code runs only when a param of this cluster is on the battlefield / in
hand, or behind a deck constant stamped from these params. Every other deck stays byte-identical (§6).

### 4.1 The type statics: Enchanted Evening + Opalescence (one shared refresh)

`RefreshTypeStatics(GameState&)`, the Heliod shape, presence-gated on `deck_has_type_statics` (stamped from either
param):
1. Restore: for every permanent this refresh changed last time (a new `Permanent` bit, e.g.
   `type_static_mask` bits {added Enchantment, added Creature, set base P/T}), undo exactly what it added, restoring the
   saved base P/T (saved in two small fields on the permanent the first time it overrode them; a definition-less token
   such as a spawn body has its printed P/T only on its copy, so it must be saved, not re-read).
2. Layer 4, EE first (CR 613.8: Opalescence DEPENDS on EE, because EE changes what Opalescence applies to, so EE
   applies first whatever the timestamps; EE does not depend on Opalescence): while any EE-param permanent is on the
   battlefield, add `Enchantment` to every permanent's copy (both sides; lands; tokens; EE itself is one already).
3. Layer 4 then 7b, Opalescence: for each Opalescence-param permanent O, every OTHER permanent that is a non-Aura
   enchantment (after step 2) gains `Creature` and base P/T = its MV (overwriting a creature's printed P/T: layer 7b
   base setting). Two Opalescences animate each other (the ruling); one never animates itself.
4. No timestamp ordering is needed among these: nothing in this pool interacts with them in the same layers (no
   Humility, no other 7b setter). If one is ever added, the Scryfall Humility ruling above applies.

Call sites: the Heliod chokepoint list (§2.1), PLUS three entry sites that bypass the enter cascade:
`OpponentLandEnters` (shared by both worlds), the executor spawn site (`GameEngine::UntapStep`) and its rollout twin
(`TurnSolver` ~85816). Each call that can create a 0-toughness creature is followed by the shared
`SweepZeroToughnessSba` (both worlds; the rollout has no SBA pass of its own) and `SweepOrphanedAuras` (CR 704.5m:
Utopia Sprawl / Wild Growth on a dead land go to the graveyard). The dying lands go through `OnCreatureDies` (they are
creatures), to their OWNER's graveyard.

What the opponent's turn must do differently under EE + Opalescence:
* Its land drop enters, dies (SBA), and never taps. Its FETCHLAND dies before it could be activated: in
  `PlayLeftmostLandFrom`, a lands-die-on-entry static joins the Blood Sun gate (`LandNonManaAbilitiesSuppressed`) so
  the fetch path is not taken and the fetch goes to the graveyard via `OpponentLandEnters`. A Karoo's return trigger
  finds no land; Lake of the Dead's replacement finds no Swamp; an ETB life gain still resolves (a trigger survives its
  source).
* `opponent_played_land_this_turn` stays true (it played one): the annihilator veto reads it, harmlessly.

Mana value used by Opalescence: §2.1 (copy's mana cost; X = 0; tokens / lands 0).

EE alone in goldfish: changes nothing except what reads "enchantment": Calming Verse, Force of Vigor, Sterling Grove's
shroud (every other permanent we control, lands included, gains shroud: §4.4 / §7 for the targeting consequence), and
any "whenever an enchantment enters" watcher (none in this list; CR 603.6d would apply EE to an entering permanent,
which a future constellation deck must respect). Enchantress's Presence counts enchantment SPELLS, which EE does not
touch (it affects permanents only).

Opalescence without EE: animates our own non-Aura enchantments (Presence 3/3, Sterling Grove 2/2, Exploration 1/1,
O-Ring 3/3, Wave 4/4, EE 5/5, another Opalescence 4/4): the deck's damage source, with control-duration summoning
sickness. The opponent has no enchantment, so nothing on its side changes.

### 4.2 The pre-float at the lock: a sound dominance fold (EE + Opalescence)

When a resolution is about to make the land-death static active for OUR side (the second of EE / Opalescence
resolving, or anything else that completes the pair), each untapped land we control with a mana ability is tapped
into `floating_mana` FIRST, at the shared resolution site in both worlds, before `RefreshTypeStatics`. Rules: we may
activate mana abilities while the spell is on the stack and we hold priority (CR 605.3a); floating mana lasts to the
end of the step/phase (CR 106.4 / 500.4) and empties with no mana burn. This is the Saproling Burst doomed-body
pre-float argument: every such land is about to die untapped; tapping it only ADDS mana to this phase and changes
nothing else (it was not going to untap, attack or pay anything after it died). Utopia Sprawl / Wild Growth's extra
mana rides each tap.

Colour: a single-colour land floats its colour. A dual (Temple Garden, Sunpetal Grove) floats as `wild`, which is EXACT
only when every coloured pip any card of the deck can still need is within the dual's colours. Stamp
`deck_float_wild_exact` per deck (every cost pip in {G, W}, hybrids by either half): true for this list (EE's {W/U}
pays as W; every other cost is G / W / generic). Where the stamp is false the colour split becomes a searched axis
(one variant per number of duals floated as each colour); not needed for this deck, so not built yet.

NOT folded: cracking a fetchland before the lock. Its thinning changes the library order, so it is not dominated; it
stays the searched action it already is (the "fetch-hold review owed" item in the ledger). An uncracked fetchland
dies with the rest.

Two consequences the implementer must handle:
* The mana PLANNER (affordability memos, `SubsetPayableSequential`, breakpoint feasibility) assumes land sources
  persist across a plan. After the lock they survive only as this phase's float. A plan that completes the lock in
  main 1 and spends land mana in main 2 is infeasible; the executor and rollout realise that correctly, but the planner
  must not over-promise (an affordability gate: after a lock-completing cast, land supply = the float, phase-limited).
  Mid-phase memo keys must already include `floating_mana` (the rituals rely on it): confirm.
* The rollout's greedy cast order beyond the horizon may complete the lock early. The pre-float keeps the same phase's
  later casts payable, which removes most of the damage. A provider rollout-policy refinement (cast the lock piece last
  in its phase) is allowed (a playout policy beyond the horizon) and is a measured follow-up.

### 4.3 Calming Verse

Clauses: (1) destroy all enchantments we don't control; (2) THEN, checked during resolution, if we control an untapped
land, destroy all enchantments we control.

Resolver `ResolveCalmingVerse` (shared; instant/sorcery resolution site of both worlds: `EffectHandler::ResolveImpl`
and `TurnSolver` apply_one, the Worldfire wiring): clause 1 destroys every opposing permanent that is an enchantment on
its copy (under EE: all of them; tokens cease to exist; indestructible stays); clears the virtual board (§3.4.3);
then clause 2 tests `any own permanent with IsLand() && !tapped` (an untapped fetchland counts: it is an untapped land)
and if so destroys every own enchantment (under EE: everything we control, lands included). Destructions go through the
shared death / leave helpers (so the Wave's LTB fires, §4.4), then `RefreshTypeStatics` + the sweeps.

**The tap-all choice is a two-variant cast axis, a sound fold of 2^n tap subsets into two.** Before the Verse resolves
we may tap any untapped lands for mana (it floats). Only one bit of the tap subset matters to clause 2: whether ANY
untapped land remains. Among the subsets that leave one untapped, leaving everything untapped weakly dominates (an
untapped land is strictly more flexible than its float: it can still pay this phase AND later phases); among the
subsets that leave none, there is only one. So the cast offers exactly two variants:
* **KEEP** (`Action::verse_tap_all = true`): before resolution, tap every untapped land with a mana ability into the
  float (the §4.2 machinery and colour rule), and crack nothing: an untapped fetchland makes KEEP impossible unless
  cracked first, so KEEP is offered only when no untapped fetchland remains (cracking it is the existing searched
  action, taken earlier in the line). Clause 2 then finds no untapped land: our board survives.
* **SWEEP-SELF** (default false): lands as they are; clause 2 destroys our enchantments if one is untapped.
  With EE that is everything we own. It is legal and almost never right; the engine must still offer it (NO-GREEDY:
  the engine makes no heuristic pick in the window). A provider may prune it as a tested one-option restriction; the
  agent default is NOT to prune (one extra branch per Verse).
When no land of ours is untapped after paying, the two variants coincide: offer one. Under EE + Opalescence we control
no land, so a Verse cast off the float always keeps our board (no untapped land): Opalescence then Verse in one phase
is the cleanest full wipe.

Without EE: clause 1 has no target in goldfish (the opponent has no enchantment; it clears virtual enchantment cards),
so Verse is a no-op (KEEP) or a self-wrath (SWEEP-SELF). Tier 3 (new resolver + a new cast-variant axis), risk low.

### 4.4 Parallax Wave

Clauses: (1) Fading 5; (2) "Remove a fade counter: exile target creature"; (3) LTB: each player returns all cards they
own exiled with it.

1. Fading 5: `fading_counters: 5` (existing; enter sites both worlds; `PerformUpkeepFading`). Tier 1.
2. Activation: a new `PermAbilityMode::FadeExileCreature` beside FadeSaproling in the shared `ApplyPermAbility` (no
   {T}, no mana; cost = one counter, paid first; legal the turn it lands). TARGET = any creature on the battlefield,
   either side, INCLUDING the Wave itself when it is a creature (under Opalescence), excluding a creature with
   hexproof we do not control and anything with shroud (Sterling Grove: §7). The target is a searched plan axis (the
   Ulamog cast-trigger target shape: one plan variant per target class, identical tokens folded). Resolution: if the
   target is a TOKEN it ceases to exist (CR 111.7 / 704.5d), so exiling an opponent spawn body is permanent; if a CARD,
   it moves to exile and its number is appended to the Wave's linked list.
3. Linked exile LIST: `Permanent::linked_exile_numbers` (a small vector, or a fixed array of int16 with a count, in a
   Permanent hole; folded nonzero-gated, future-determining). On the Wave leaving the battlefield (any route: the
   shared leave helpers, fading's `SacrificePermanentAt`, a destroy, its own exile) every listed card returns to the
   battlefield under its OWNER's control as a new object (CR 400.7), through the universal enter cascade (so it is
   summoning-sick, re-runs its ETBs, and `RefreshTypeStatics` applies). Brutal Cathar's `ReturnLinkedExile` is the
   one-card precedent to generalise.

**The self-exile (CR 607.2a linked abilities + CR 400.7), stated and checked:** the exile ability and the LTB ability
are linked; "cards exiled with it" are the cards that THIS OBJECT's exile ability exiled. Under Opalescence the Wave is
a creature and may target itself. With activations A1..Ak on the stack targeting other creatures and a last one
targeting the Wave: the self-target resolves first, the Wave is exiled by its own ability (so it is itself a card
exiled with it), its LTB trigger resolves and returns every card exiled with that object, the Wave card included, as a
NEW object with five fade counters. A1..Ak then resolve (an ability on the stack exists independently of its source,
CR 113.7a) and exile their targets, linked to the OLD object, whose LTB has already resolved: those exiles are
permanent. Repeating with the new Wave exiles any number of creatures in one turn. This is the known
Opalescence + Parallax Wave lock, and it is the USER's ruling 1. The returned Wave is a new object: summoning-sick on
the turn it returns, so a loop on our turn before combat costs its attack (ruling 3's attacker).

Engine treatment (literal where it matters, abstract where only the fairness check reads it):
* Literal: the target menu includes the Wave itself under Opalescence, and its own-target resolution returns it with
  five counters via the linked list (the self-card is in the list). The engine resolves each activation immediately
  (no stack), so "activations on the stack" is modelled as: a planned activation batch [targets..., self] applies the
  non-self exiles first, recording them in a TEMPORARY list that the self-exile's return does NOT return (they belong
  to the old object), then the self-exile. One searched plan action "Wave loop (targets T1..Tk)" expresses it. In
  goldfish the real targets are few (spawn tokens, already permanent; opponent lands are dead under EE + Opalescence),
  so this matters for the fade reset and for the USER's line, not for clearing the board.
* Sterling Grove ("other enchantments you control have shroud") stops the Wave targeting ITSELF until each Grove has
  been exiled by it (a Grove does not shroud itself, and it is a 2/2 creature under Opalescence). So a loop needs
  `fade_counters >= 1 + G` with `G` Groves out, each cycle nets `4 - G` permanent exiles and returns the Groves, and
  `G = 4` nets nothing. The deck-default sweep (§4.4.1) runs this loop at every opponent end step; it also resets the
  Wave to five counters, so fading never kills a Wave that sits beside an Opalescence.
* Timing fold (against a passive opponent; sound): a self-reset on our own turn before combat is dominated by the
  end-step sweep (it only costs the Wave's attack); in main 2 it equals the sweep's reset. An activation targeting OUR OWN creature
  with no enter/leave payoff (anything but the self-reset, and a Grove as its enabler) loses a counter and an attacker
  and returns the card summoning-sick: dominated by not activating. O-Ring under the Wave is the one own target with a
  re-ETB (it re-exiles on return); in goldfish it has nothing new to exile, so it is FLAGGED, not invented.
* Without Opalescence: the Wave exiles only real creatures, i.e. opponent spawn bodies (permanently, as tokens). Its
  only goldfish value is free-turn condition 2 on a board the deck has stripped of lands while a spawn remains, and
  creature-as-resource cards this list does not run. No own-creature lines exist (we have no creature without
  Opalescence). Flag, do not invent.

Tier 3 (new activation mode + linked list + self-target), risk medium.

#### 4.4.1 The deck default: the Wave + Opalescence sweep (USER ruling 7)

WHAT: with a Parallax Wave AND an Opalescence on our battlefield (tapped or not is irrelevant: the ability has no
{T}), this deck's provider exiles EVERY opponent creature, real and virtual, through the self-exile loop. With EE also out, every
opponent permanent is a creature, so the sweep takes all of them; the opponent's later land drops die as 0/0s and are
never its business. No counter budgeting for opponent targets: the loop is unlimited.

WHERE (one shared function, both worlds, lockstep): `PerformWaveOpalSweep(GameState&)` in `SpellEffects.h`, called in
BOTH `opponent_turn` lambdas (`GameEngine::RunTurnFrom` and `TurnSolver`'s `SimulateEndAndStartNextTurn`) at the
opponent's END STEP: after `ResolvePassiveOpponentLandTurn` and `freeturn::NoteVirtualDeployments` (so its land drop
and its virtual deployments of that turn are already on the board), before the rest of the end-step window (Sire's
check and discard, `PerformOpponentEndStepFaceUp`, the Force of Vigor entry) and therefore before
`freeturn::NoteOpponentTurnEnd`. Inside `PassiveOpponentTurnScope` (an activated ability at instant speed on the
opponent's turn: legal, CR 602.2 / 117.1). Gate (first line): the deck constant `deck_has_wave_opal_sweep` (stamped
from the provider's rule; false for every other deck) AND a Wave-param and an Opalescence-param permanent under our
control AND at least one opponent creature, real or virtual. It runs whether or not `MTG_FREE_TURNS` is on (it is a
play rule); its virtual half acts only where the fairness check is armed.

WHEN, and why that one point answers "whenever a new opponent creature appears": new opponent creatures appear only
(a) at our untap (scheduled spawns), (b) when EE or Opalescence resolves (animating opponent permanents; under EE +
Opalescence the lands and MV-0 tokens die at once, so only MV >= 1 virtual permanents remain), (c) in the opponent's
main phase (its virtual deployments; possible only while it has lands, i.e. without EE). Every one of these precedes
the opponent's end step, and the only thing in this engine that READS opponent creatures between their appearance and
that end step is nothing at all: the passive opponent never blocks or attacks, and no card of this list counts or
targets opposing creatures as a resource. So the end-step sweep removes each of them before the only reader that
matters (the lock check), and it never costs the Wave its attack: a loop on our own turn before combat would return the
Wave as a new, summoning-sick object (CR 302.6), which ruling 3's attacker cannot afford. An earlier sweep on our turn
is therefore weakly dominated by this one (identical lock result, possibly one fewer attacker), so it is not added.
Explicit Wave activations on our turn stay ordinary searched plan actions (§4.4 item 2); the default governs only the
sweep point.

HOW (the loop, per CR 607.2a / 400.7 as stated in §4.4): let `G` = our Sterling Groves on the battlefield, `c` = the
Wave's fade counters.
1. If `G == 4` or `c < 1 + G`: the Wave is not ABLE; the sweep does nothing (counters untouched). (`c < 1 + G` needs a
   Grove-heavy board on a Wave that missed several sweeps; with the sweep running every opponent end step the Wave
   sits at 5 at our upkeep and 4 after it.)
2. Otherwise repeat while a targetable opponent creature remains: exile each Grove with the Wave (one counter each;
   they join the CURRENT object's linked list), exile up to `c - G - 1` opponent creatures (tokens cease to exist,
   cards go to exile PERMANENTLY: they are recorded in the batch's temporary list, never in the linked list), then
   the Wave exiles itself (the last counter); its LTB returns every card in its linked list (the Groves and the Wave
   card) as new objects; the new Wave enters with five fade counters (CR 702.32: "enters with"); continue with
   `c = 5`. Each full cycle nets `4 - G` permanent exiles. A virtual creature counts as a target exactly as a real
   one (it costs a counter in the cycle accounting) and its bit is cleared; a hexproof one is skipped.
3. COST and END STATE: the old Wave object and every opponent creature are gone for good (exiled cards stay in exile,
   tokens no longer exist, virtual bits clear); a NEW Wave with five fade counters, `entered_this_turn` set during the
   opponent's turn and cleared at our untap, so it attacks on our next turn (it has been under our control since that
   turn began); each Grove likewise returns as a new object and can attack next turn. The new Wave's linked list is
   empty. Anything OUR earlier explicit activations had exiled with the OLD object returns too (its LTB returns every
   card exiled with it), which is the rules result and is never a loss in this list (only our own cards could be in
   it). One play event (real play only): "Parallax Wave + Opalescence: exiles N opponent creatures; Wave returns with 5
   fade counters".
4. After the sweep: `RefreshTypeStatics` + `SweepZeroToughnessSba` (a returned Grove under EE + Opalescence is a 2/2,
   it survives; nothing should die, the call is for safety and lockstep).

THE LOCK CHECK THEN: under EE + Opalescence + an able Wave, at `NoteOpponentTurnEnd` the opponent has no land (its drop
died), no creature on the board (the sweep), no virtual creature except hexproof ones, and no spawn that can appear (no
land, `SpawnMayAppear` false; and spawns are scheduled for turns 1-3 only). `LockHolds` returns true. Under Wave +
Opalescence WITHOUT EE the sweep still runs (ruling 7's default) but the opponent keeps its lands and nonland hand, so
condition 1 fails: no free turn, as expected.

HUMAN PLAY: the sweep is the deck's default, so the viewer surfaces it rather than silently applying it: one entry
"Parallax Wave loop: exile all opponent creatures" in the end-step window's new `eot_cast` decision (§5), selected by
default, so a human may decline. No lever: it is the USER's rule, adopted as the deck default (an env lever would be a
default-off capability flag, which CLAUDE.md forbids).

### 4.5 Force of Vigor

Clauses: (1) alt cost: if it is not our turn, exile a green card from hand instead of paying {2}{G}{G}; (2) destroy up to
two target artifacts and/or enchantments.

* Clause 2 (Tier 2): `destroy_up_to_artifacts_enchantments: 2`, shared resolver at the instant resolution site of both
  worlds. Targets: 0, 1 or 2 DISTINCT permanents that are artifacts or enchantments on their copy, either side (under
  EE: every opponent land and spawn; ours too, though dominated in this list except as noted in §7). Searched target
  axis (the Ulamog shape; tokens of one class folded). Indestructible survives. "Up to" makes 0 legal (a FoV cast
  only to trigger nothing is dominated; keep it legal, it is one variant).
* Hard cast on our turn {2}{G}{G}: a normal main-phase instant cast (main-phase-only fold, as Brainstorm's ledger
  argues: against a passive opponent an instant on our turn is equivalent to a main-phase cast).
* Clause 1 (Tier 3): the pitch is legal only off our turn, i.e. inside the simulated opponent turn
  (`t_passive_opponent_turn > 0`). The window: **the opponent END-STEP window** (`PerformOpponentEndStepFaceUp`'s
  menu), after its land drop, its virtual deployments and the Wave + Opalescence sweep (§4.4.1), and before
  `NoteOpponentTurnEnd`. A new menu digit / entry kind
  "cast Force of Vigor": {skip} or {pitch card class (identity-folded green hand cards other than this FoV; another
  FoV copy is legal) or hard cast with lands we left untapped} x {targets}. Searched as a `BoundaryFan` digit at the
  turn boundary exactly as the morph flip is (`ChooseRealCrossingFaceUp`'s shape), pinned on the plan of the turn that
  crossed, consumed in both `opponent_turn` lambdas (lockstep), human-play prompt (§5). Beyond the horizon the
  provider's front entry decides (the permitted playout greedy).
* What it buys: with EE in play, the land the opponent just replayed after a Verse (one per head) dies before the lock
  check, turning a "not free" turn of (A) into a free one (§3.1). Without the window the deck loses exactly that:
  FoV becomes a 4-mana sorcery-speed "destroy two lands" whose targets the opponent replaces on its next turn, and its
  free pitch never happens. Honest tier: **Tier 3, risk high** (a new BoundaryFan digit, the end-step menu, a human
  prompt); it is implementable, so it is NOT a deferral candidate.

## 5. Viewer decisions (analyze-deck 2c-ter)

| card | choice | bucket | mechanism |
|---|---|---|---|
| Enchanted Evening | none (static) | -- | -- |
| Opalescence | none (static); new attackers | A | existing `attackers` decision |
| Calming Verse | KEEP vs SWEEP-SELF (tap-all first or not) | A | `main_phase` plan variants (label them "tap all lands first" / "leave lands") |
| Calming Verse / lock pre-float | dual-land float colour | none for this deck | `wild` under `deck_float_wild_exact`; a colour chooser (B) only for a deck where the stamp fails |
| Parallax Wave | which creature to exile (incl. itself under Opalescence); the loop batch | A | `main_phase` plan variants per target class; the human-play provider must return EVERY legal target unpruned (the dominance folds of §4.4 are search-side folds and must not narrow human play, or must be disclosed) |
| Parallax Wave | LTB return | -- | no choice (all return) |
| Parallax Wave + Opalescence | the deck-default end-step sweep (ruling 7) | **B** | an entry of the new `eot_cast` end-step decision (below), selected by default; declining skips the sweep |
| Force of Vigor (our turn) | 0-2 targets | A | the `target` decision via `EnumerateTargetSets` (the multi-target row) |
| Force of Vigor (opponent end step) | cast or not; pitch card; targets | **B** | new `eot_cast` decision (chooser `g_play_eot_cast_chooser` in GameLogger.h, nulled in `RevealLogPause`; call site in `PerformOpponentEndStepFaceUp`; `Write...DecisionJson` + `RunClaudePlay`; GUI branch with card images), targets reuse `target`; add the row to `tools/play/DECISIONS.md` and the param to `audit_viewer_decisions.py`'s manifest |
| Oblivion Ring (agent A) | target | A | `target`; under EE nothing changes ("nonland"), but Sterling Grove's shroud removes our own permanents from its legal set |

Every target menu must honour Sterling Grove's shroud on OUR permanents (§7).

## 6. Implementation order, tiers, risk, byte-identity

| step | change | tier | risk | gate (other decks) |
|---|---|---|---|---|
| 1 | `RefreshTypeStatics` (EE + Opalescence), chokepoint calls, the three bypass entry sites, `SweepZeroToughnessSba` + `SweepOrphanedAuras` after each, opponent fetch suppression under land-death, `Permanent` restore bits + saved P/T (in existing holes / bitfields; check `sizeof(Permanent)`), audit of the 23 definition-reading battlefield sites | 3 | HIGH (many sites, both worlds) | `deck_has_type_statics` stamp + param presence on the battlefield |
| 2 | Pre-float at the lock (§4.2), `deck_float_wild_exact`, planner phase-boundary gate | 3 | medium-high (planner) | fires only when the land-death static activates |
| 3 | Calming Verse resolver + KEEP / SWEEP-SELF variant axis | 3 | low | param |
| 4 | Parallax Wave: activation mode, target axis incl. self, linked list + LTB return, loop batch, dominance folds; the deck-default end-step sweep `PerformWaveOpalSweep` (ruling 7) in both `opponent_turn` lambdas | 3 | medium | param; `deck_has_wave_opal_sweep` stamp |
| 5 | Force of Vigor clause 2 (hard cast) | 2 | low | param |
| 6 | Free-turn integration: trait combination in `DeckMayLockOpponent`, `DeckLockNeedsLandStrip` false, `MTG_FREE_TURN_EE_FAIR` virtual board (record / clear by Verse and by the sweep / veto), key folds | 3 | medium | `deck_has_ee_lock` stamp; masks are zero elsewhere so no key changes |
| 7 | Force of Vigor clause 1: the end-step window digit + `eot_cast` viewer type | 3 | HIGH | FoV param in hand; the menu is empty otherwise |
| 8 | Collapse folds: (a) a land play under EE + Opalescence is dominated by holding the land when the deck has no landfall / land-enters / land-dies / graveyard-count reader (this list has none): the land only changes zones, and a held land keeps its value if the lock ever breaks; (b) the Wave own-target dominance of §4.4 | 2 | low | stamps |
| 9 | Viewer wiring (§5), DECISIONS.md, audit manifest | 2-3 | low | human play only |

Validation per step: unit tests (each clause; EE/Opalescence dependency; two Opalescences; summoning sickness; dying
lands drop their Auras; opponent land drop + fetch die; spawn dies; Verse both variants with an untapped fetchland;
Wave self-loop exiles permanently and returns with 5 counters; Wave LTB returns its list; the end-step sweep with 0, 1
and 4 Groves, and its lock result with EE in play; FoV off-turn only), scenario
fixtures in `test/scenarios/` for the three lock configurations (each asserting the free-turn event), a
`MTG_FREE_TURN_REACH_AUDIT` run on this deck (must not certify: step 6 makes the certificate inapplicable), smoke.
**Expected effect on other decks: none.** Every new path is gated on a param of this cluster or a deck constant
stamped from them; no shared default changes; the new key folds are nonzero-gated on fields that stay zero elsewhere.
Smoke must show play-changed = 0 on every other deck. Two things could break that and must be watched: a change to
`sizeof(Permanent)` (perf only, not play) and any edit to the shared enter / leave helpers that is not behind the
presence gate (keep the refresh call's first line `if (!state.deck_has_type_statics) return;`).

The lock's effect on THIS deck is large by design (a win credited on the lock round), and the turn-edge prune
stand-down costs it search units; both belong in the deck's own measurement, not in other decks' GT.

## 7. Cross-agent notes (for the agent implementing Sterling Grove / O-Ring / Presence)

* **Sterling Grove's shroud is NOT goldfish-inert in this list.** "Other enchantments you control have shroud": under
  EE that is every other permanent we control, lands included. It removes our own permanents from OUR targeting:
  Parallax Wave (its self-target, hence the `1 + G` counter rule of §4.4), Force of Vigor, Oblivion Ring. A Grove
  does not shroud itself. The engine has no Shroud keyword; the cleanest model is a predicate
  `HasShroudFromStatics(perm, state)` read by every target-legality filter this cluster adds (the existing
  `CreatureHasShroud` in TurnSolver may be the hook to extend).
* Oblivion Ring under Opalescence is a 3/3 creature; under the Wave it is a legal own target (its re-ETB is the one
  own-target line with a payoff, flagged in §4.4).
* Enchantress's Presence draws on enchantment SPELLS; EE does not make spells enchantments.

## 8. Open USER questions

Each is an area where the USER's input decides the work; the agent default is taken meanwhile.

**Q1. Should Force of Vigor and Oblivion Ring remove VIRTUAL permanents in the fairness check?** The check (§3.4)
records the nonland permanents a real opponent could have cast; only Calming Verse (all of them, under EE) and the
Wave + Opalescence sweep (every targetable creature, ruling 7) clear them. A real Force of Vigor under EE destroys two
of them and a real O-Ring exiles one, but crediting that means choosing targets on a board that does not exist (a new
virtual target axis in the end-step window and on O-Ring's ETB). Without it, EE + Opalescence with no Wave, one or two
leftover virtual permanents and a FoV in hand earns no free turn though a real game would. **Default: not credited**
(the conservative under-estimate, the Sire precedent). Alternative: credit FoV / O-Ring against the virtual board with
a fixed target rule (highest MV creature first), a pick made inside the check only, never in the search.

**Q2. Does a virtual HEXPROOF creature survive the Wave + Opalescence sweep?** Ruling 1 says the combo exiles "any
creature", and ruling 7 says the default is to exile all opponent creatures. By the rules the Wave cannot target a
hexproof creature, so a real opponent that cast one before the lock keeps it (an MV/MV creature) and condition 2 fails
every turn. The engine knows `Hexproof` but has no Shroud / Protection keywords. **Default: faithful: a hexproof virtual
creature is skipped by the sweep and vetoes the lock; shroud / protection are not modelled (treated as targetable),
disclosed.** Alternative: the blanket reading (the sweep clears everything).

## 9. Information (not questions)

* Verse + EE: a turn the opponent ends land-less is free; a turn it replays a land holding a nonland is not, unless an
  end-step Force of Vigor (step 7) kills that land. (USER rulings 5 and 6 settle this.)
* Under EE + Opalescence + Wave, once the lock holds every later turn is free, so the deck's reported win turn is the
  round the lock completed. With the fairness check ON, EE + Opalescence WITHOUT a Wave will often not be credited
  (a pool opponent that has had lands for several turns usually could have cast something), unless a Calming Verse
  wiped the board shortly before (ruling 5's "no major gap", ruling 6's test).
* The sweep runs at the opponent's END STEP, not the moment Wave + Opalescence meet on our turn: every opponent creature
  appears before that end step, nothing reads it in between, and a loop on our own turn before combat would make the
  Wave a new, summoning-sick object and cost ruling 3's attack. The lock result is identical (§4.4.1).
* Sterling Grove's shroud limits the Wave's self-exile: the loop needs `1 + G` fade counters with `G` Groves out and
  nets `4 - G` exiles a cycle; with all four Groves out it cannot net an exile. This is the rules, modelled as such
  (§4.4); it matters only on Grove-heavy boards.
* The virtual opponent deploys, each turn, the castable nonland permanents of its hand left to right within that turn's
  land count (one real mana budget per turn), the reading of ruling 6's "could they actually have cast". The Sire and
  annihilator checks count each card on its own instead; this check is stricter about mana on purpose.
* The deck's own mana is gone after the EE + Opalescence lock except for the phase's float; that is the real rules
  cost of the combo and the search has to sequence around it (§4.2).
* Step 6 removes the reach certificate for this deck; its search pays the full turn-edge stand-down.
* For the agent implementing Sterling Grove: its shroud is not goldfish-inert here (§7).
