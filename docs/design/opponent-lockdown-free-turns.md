# Opponent-lockdown "free turns" (USER proposal, 2026-11-21)

Status: PROPOSED by the USER, being built for the Worldfire analysis behind a flag. The
open questions below are the USER's to settle; each one lists the default the agent took while
building, and that default is PROVISIONAL until the USER answers.

## The USER's words (verbatim)

> I have been thinking about a new way to count as "extra turns". This is my current idea: if the
> passive opponent has both 1. no lands on the field or no non-lands in hand 2. no creatures out at
> the end of their turn this would count as a free turn for the player. The intent is to have
> lockdown combos like this one receive a small benefit for how much it shuts down the opponent.

> This rule matters a lot for this deck, because no deck can meet those conditions a turn after
> worldfire.

> And it is close to how things behave in real games. Usually casting Worldfire with something
> suspended is a win. (because there is no room for the opponent to respond)

## The rule

At the END of each opponent turn, the opponent is LOCKED when both hold:

1. (no lands on the opponent's battlefield) OR (no nonland cards in the opponent's hand), and
2. no creatures on the opponent's battlefield.

Each locked opponent turn is a FREE TURN for us: the round counter (`GameState::turn_number`, the
unit every reported win turn and every search comparison uses -- USER ruling 2026-10-26 for extra
turns) does not advance across it. Unlike an extra turn, the opponent DID take its turn: it draws,
plays its land by its deterministic policy, and its scheduled spawns (if any) arrive.

Why the turn after Worldfire always qualifies: Worldfire leaves the opponent with no permanents and
no hand, and the opponent draws exactly one card. If it is a land (played), there is no nonland in
hand; if a nonland, there are no lands; and one card on <= 1 land cannot have put a creature out.
So Worldfire + a suspended finisher (Rift Bolt arriving next upkeep) is credited as a win on the
Worldfire turn itself -- the USER's "usually a win" in real play.

## What the engine already has

* A real opponent library + 7-card opening hand for EVERY deck since 2026-11-17
  (`opponentdeck::Deal` from `GoldFishRunner::SetupGame`; seed-selected from the 50-deck pool; the
  opponent plays the LEFTMOST land of its hand each turn, `PlayLeftmostLand`, and never mulligans or
  casts a nonland). The opponent's turn is simulated at the END OF OUR TURN (its draw + land play,
  `core/OpponentDeck.h`), so "the end of their turn" is that slot, after its draw, land and any
  scheduled spawn. Only `--legacy-passive-opponent-lands` replays use the old three-placeholder board.
* Scheduled creature spawns (`GoldFishRunner::PopulateOpponentSpawns`): all arrive on turns 1-3.
  They are opponent creatures for condition 2.
* Extra-turn accounting (`extra_turns_taken`, `TurnSeq()` = turns taken, `turn_number` = round).

## Open questions (USER), with the provisional default taken

1. **Scope -- USER 2026-11-21: make it GENERAL, but mana screw must not trigger it.** The USER's
   words: *"It would be nice if it was more general, though. I agree that mana screw causing this
   would be bad. ... maybe it could be something like the opponent needing to show that they can
   remove things that are out to make this happen rather than it happening automatically? For
   example, we could put out automatic lands equal to the difference if they have less than 2 in
   hand?"* and *"That way there is a real hurdle even with mana screw."*
   Measured over the 50-deck opponent pool (hypergeometric, opening 7): P(< 2 lands) = 20.4%
   (P(0) = 3.9%; Stompy Surprise, 13 lands, 52%); P(7 lands, i.e. no nonland) = 1.0%.
   DESIGN BEING BUILT (flags default OFF until the USER adopts):
   * **Screw hurdle (USER), REPLACE-not-add (USER 2026-11-21: *"I wonder if we need to actually do
     the replacement for lands as well. We could very well do that and it would keep the 1 land a
     turn rhythm."*).** An opening hand with n < 2 lands (MDFC land faces count as lands) swaps its
     2 - n RIGHTMOST nonlands for BASIC lands, each basic taking the slot it replaces (the rest of the
     hand keeps its order; the leftmost-land policy then plays them on its first turns -- one land a
     turn, no land ahead on the battlefield). Displaced nonlands leave the game (library untouched;
     hand stays 7). The basic is the list's most common basic, a real basic card (USER: *"Let's make
     the added lands basics to minimize changes."*); the four lists with NO basics -- Eldrazi Displacer
     Flicker, Fluctuator, Galadriel, Prevent Damage -- get the basic of the colour their lands produce
     most (USER approved: *"That sounds good."*). Lands can then reach zero only through an effect of
     ours.
   * **Flood hurdle (agent PROPOSAL, USER 2026-11-21: *"I'm not so sure about the flood case, but I
     guess we do need something there also. The awkward part is that if you add 2 placeholders to
     the hand it would overflow?"*; replace-not-add APPROVED: *"Sure, the replace-not-add makes
     sense."*).** Adding WOULD overflow: the passive opponent's cleanup
     (`opponentdeck::EndOfTurnCleanup`) discards the LEFTMOST card down to 7. So REPLACE instead: an
     opening hand with m < 2 nonlands swaps 2 - m of its lands for uncastable placeholder nonlands,
     placed at the RIGHT end of the hand (the leftmost-first cleanup sheds real cards first). The
     displaced lands leave the game (library untouched, so mill / library order are unaffected; the
     hand stays 7). Discard / Worldfire remove placeholders like any card.
   * The free-turn rule then applies to EVERY deck. The suite measurement reports which decks' play
     the top-up moves (readers of opponent lands) and how many free turns non-lock decks receive
     (expected ~0; anything else is a hole in the hurdle).
2. **Reading of condition 1.** DEFAULT: "(no lands on battlefield) OR (no nonland in hand)", AND
   condition 2.
3. **2HG -- USER 2026-11-21: every opposing head must be locked** (each evaluated on its own zones):
   *"Yes, for 2HG, which makes it a bigger hurdle for some lockdown decks. I do believe the lockdown
   decks will still be undervalued by the engine, but this should still help a bit."* (The rule is a
   partial credit by design; lockdown decks stay undervalued vs real play.)
   Implementation note: both heads' lands sit on ONE battlefield under controller 1, so "this head's
   lands" is read from the card-number band (`kNumberBase` vs `kNumberBase2`, `OpponentDeck.h`); the
   creature condition is the shared board (any opposing creature fails it for every head).
4. **Cap.** DEFAULT: none; a free turn only matters in a game that is won.
5. **Search depth.** USER 2026-11-21: *"On search depth it's a tougher call. What did we do for
   Extra Turns?"* Extra turns: depth counts ROUNDS -- an extra turn costs no ply
   (`DepthAcrossTurn`, analysis-Unpredictable Cyclone.md, its Q9 -- an agent DEFAULT, never explicitly
   answered by the USER). DEFAULT NOW (revised): match it -- a free turn costs no ply. Reasons: every
   window check is `win_turn <= turn + depth - 1` in ROUNDS, so charging a ply would make a credited
   lock-then-win line a round shallower than its credit (inexact windows); post-lock positions are tiny
   (Worldfire leaves both sides near-empty), so the extra turns are cheap; and depth-in-rounds keeps a
   lock-then-win line always inside the window (budget-may-reprice rule). The A/B reports search units
   so a long-lock cost shows up. PROVISIONAL; Q9 for extra turns and this should be settled together.

## Measurement plan

Worldfire is reported under BOTH metrics (raw rounds and free-turn credited) so the USER can see
what the rule buys. The rule ships default-on for armed decks only after the USER answers (1).

## Found while sizing the hurdle (USER 2026-11-21): the opponent never plays an MDFC's land face

USER: *"Stompy Surprise could play the reverse side of the MFDC card Turntimber Symbiosis as
Turntimber, Serpentine Wood."* Confirmed: `opponentdeck::PlayLeftmostLandFrom`
(`core/OpponentDeck.h`) picks the leftmost card whose FRONT face `IsLand()`, so a spell//land MDFC
is never played as its land, although the engine's own `PlayableAsLand` / `LandFaceDefOf`
(`core/SpellEffects.h`) know the back face. Affected pool lists: Stompy Surprise (Turntimber
Symbiosis x4), Merfolk (Sink into Stupor x2), Morph (Legion Leadership x4); Pathways are lands on the
front and unaffected. FIX (owed, lands with the free-turn change): pick by `PlayableAsLand`, enter the
land face (the passive opponent declines Turntimber's optional 3 life -> tapped, as it declines a
shock), and count land faces as lands for the screw hurdle. It moves opponent land play in ~6% of
games, so any deck that reads opponent lands moves: an engine fix through the normal accept flow.
With land faces counted, P(opening 7 has < 2 lands) = 19.7% (P(0) = 3.7%; worst Kitty Equipment
39%, Stompy Surprise 35%); P(no nonland) = 1.0%.

## Found 2026-11-21 (USER, in the play viewer on another machine): opponent lands skip their triggers

USER: *"the opponent can play lands without the triggers. So bouncelands don't bounce. This means that
they play them out faster than they should be able to. With this new rule we may need to replace one
of the lands with all-bounceland (or all lands that require others out) with a basic."* -- *"(for when
this problem is fixed)"*.

Confirmed: `opponentdeck::PlayLeftmostLandFrom` models enters-tapped and fetch cracks only. Lands in the
pool that NEED ANOTHER LAND and currently stick anyway:
* `etb_bounce_land` (Karoo): Azorius Chancery (Bruna, Creature Giving, Spirits), Gruul Turf (Dragons,
  Landfall, Landfall Elementals, Mirrorwing Dragon, Morph), Izzet Boilerworks (Hinata), Orzhov Basilica
  (Martyr Lifegain, Melira Pod), Rakdos Carnarium (Minotaur) -- 9 lists.
* `etb_replace_sacrifice_land_subtype`: Lake of the Dead (Shadowborn Apostle x3) -- sacrifice a Swamp
  or it goes to the graveyard.
The viewer and the engine share this function, so one fix covers both.

FIX (owed, its own measured commit, lands with the MDFC land-face fix, BEFORE the hurdle extension):
a bounceland returns a land its controller controls -- fixed passive rule: the OLDEST other land, else
itself (a lone bounceland leaves the opponent with no land, as in real play); Lake of the Dead
sacrifices a Swamp if it controls one, else goes to the graveyard. Moves opponent land development in
games against those 10 lists -> any deck reading opponent lands moves; normal accept flow.

HURDLE EXTENSION (after that fix, per the USER): after the < 2-lands swap, if NONE of the opening
hand's lands can stay on an empty battlefield (all bouncelands / Lake of the Dead), replace the
RIGHTMOST of them with a basic (same basic choice as the screw hurdle). Post-Worldfire this is correct
as is: a drawn bounceland returns itself and the opponent stays locked.

## Implementation (2026-11-21, branch wip/freeturn; flags DEFAULT OFF -- adoption is the USER's)

Flags (`src/ai/EngineFlags.h`, shared readers): `MTG_FREE_TURNS` (default OFF) arms the credit and the
hurdles; `MTG_FREE_TURN_HURDLES` (default ON under `MTG_FREE_TURNS`, inert without it) `=0` measures the
credit without the hurdles. One definition of the rule: `src/core/FreeTurns.h`. Unit tests:
`test/unit/test_free_turns.cpp`.

* **Lock check, both worlds in lockstep.** `freeturn::NoteOpponentTurnEnd` runs right after the
  opponent's REGULAR turn in `GameEngine::RunTurnFrom` and in `TurnSolver`'s
  `SimulateEndAndStartNextTurn` (same instant, same function). Not before our extra turn (the opponent
  gets none), not on the pre-game opponent turn when we are on the draw, not after an opponent EXTRA
  turn (the regular turn that follows it is the one checked). A locked turn raises
  `GameState::pending_free_turn`; the next `AdvanceTurnCounter` consumes it: `turn_number` stays,
  `++free_turns_taken`, `in_free_turn = true`.
* **Accounting.** `TurnSeq()` counts free turns (same-turn stamps stay correct); `RawRound()` =
  `turn_number + free_turns_taken` is the uncredited round; `TurnSeqCapped` (the 4 x max_turns
  termination backstop) also covers free turns. `PlayOutFrom` plays a free turn owed at the end of round
  `max_turns`, as it does an extra turn.
* **Lock condition** (`freeturn::LockHolds`). Per head (2HG: both heads, each on its own card-number
  band): no land on the battlefield (tokens included) OR no nonland card in hand; AND no opposing
  creature on the shared board (an animated permanent counts), counting a scheduled spawn due with our
  next turn (that body is the opponent's for the turn that just ended). Only the real-opponent model
  (`opponent_plays_lands`) locks; a legacy replay never does. In hand an MDFC spell//land is a NONLAND
  card (outside the stack and battlefield an MDFC has only its front-face characteristics), while the
  hurdles count it as a LAND (USER: land faces count).
* **Spawns** key on `RawRound()` (the opponent took its turn, locked or not), so a free turn's spawns
  arrive. Identical while the flag is off.
* **Search depth**: `DepthAcrossTurn` and the rollout's entry give-back use `InSameRoundTurn()`
  (extra OR free), so a free turn costs no ply (the Q5 default, matching extra turns).
* **Turn-edge prunes.** Every prune gated on `deck_has_extra_turn` because it assumes "the next turn is
  round + 1" is now gated on `NextTurnMayShareRound()` = `deck_has_extra_turn || deck_has_free_turn`:
  EchoEdgeFold, FslEdgeNoReach, WinlessCertificate, LastPlyFold, go-off dominance, edge-tail elide; the
  leaf kill-only turn additionally requires `!deck_has_free_turn`. `deck_has_free_turn` (a deck
  constant, `StampDeckTraits`) = `MTG_FREE_TURNS` on AND `GoldFishRunner::DeckMayLockOpponent` (main +
  sideboard hold a card that strips the opponent's lands or hand; narrowed in the second pass to Worldfire,
  Sire of Insanity and annihilator, with the sideboard counted only when a main-deck wish reaches it -- see
  "Second pass" (b)). Gating per deck keeps every
  other deck's search identical; a deck the list misses still receives the credit in play (the rule is
  general), only its edge prunes stay armed, and the batch reports it with `trait=0`.
* **Keys.** `free_turns_taken` / `in_free_turn` are folded, value-gated, into `BuildSimKey`,
  `dominance::Build` and the DecisionProviders route signature. `pending_free_turn` is never live at a
  search node (raised and consumed inside one transition) and `deck_has_free_turn` is a deck constant;
  neither is folded. The four fields cost +8 bytes (`sizeof(GameState)` 1712 -> 1720, Dominance.h).
* **Hurdles** (`freeturn::ApplyOpeningHurdles`, right after `opponentdeck::Deal` in
  `GoldFishRunner::SetupGame` and in the scenario loader; every head): screw, then flood, then the
  bounceland extension, exactly as Q1 and the bounceland section describe. Hurdle cards are numbered
  inside their head's band (+9000 basics, +9100 placeholders) so `HeadOfNumber` holds. The flood
  placeholder is a synthetic DB card, "Opponent Placeholder Spell" (Sorcery {20}), registered beside the
  "Opponent Basic Land" marker; no effect reads it by name, and registering it is byte-identical
  (smoke). "Can stay alone" = not `etb_bounce_land` and no `etb_replace_sacrifice_land_subtype`.
  Hurdle basics for the four lists with no basic: Eldrazi Displacer Flicker = Forest, Fluctuator =
  Plains, Galadriel = Island, Prevent Damage = Plains.
* **Output.** A `free_turn` play event at the start of each free turn; the play-event `turn_seq`
  stamp counts extra + free turns; the decision JSON writes `free_turn` / `turn_seq` only once a free
  turn was taken (every other game's JSON is byte-identical); the viewer turnbar shows "(free k)";
  `mtg --batch` prints `[free-turn] job= gi= wt= free= raw= opp_slot= trait=` on stderr per game that
  took one.
* **Scenarios under the flag.** All 605 fixtures pass with `MTG_FREE_TURNS=1` (the hurdles apply to a
  fixture's dealt opponent too), so the tier's concurrent scenario stage needed no change.
  `worldfire_feat_suspend_bolt_kill` reports win_turn 4 instead of 5 (the credit), within its bound.

### Agent defaults (surfaced to the USER, not settled)

1. The flood hurdle removes the RIGHTMOST lands.
2. An MDFC spell//land in hand is a NONLAND card for the lock, a LAND for the hurdles.
3. A scheduled spawn due with our next turn blocks the lock.
4. After an opponent extra turn, the lock is checked once, after its following regular turn.
5. A legacy-replay board (no dealt opponent list) never locks.

### Validation (flags OFF)

Rebased onto 42d819bc0 (land-play fix + Worldfire + accepted GT). Units: 3884 / 3885 pass (the one
failure is the pre-existing `test_shadowborn_int9` "Thrumming Stone"); scenarios 605 / 605; smoke
243 / 243 PASS, per-game audit play-changed = 0 (byte-identical with the flag off).

## Measurement (2026-11-21, `MTG_FREE_TURNS=1`, hurdles on; regression tier, no accept)

One pooled run, `MTG_PGO=0 MTG_FREE_TURNS=1 test/regression.sh --regression` on 2d373428c (= 42d819bc0 +
the WIP). OFF arm: the accepted GT of 42d819bc0 (`test/gt_logs/*.wins`) and the flag-off batch log of the
same src (e6cc83f3e). Per-key table: `logs/freeturn/analysis_reg_on.txt`. Units are the work measure
(cpu ms on a shared box is noise at the +-5% level).

### (a) Worldfire: credited vs raw

| key | games | free turns (games with any) | OFF avg | ON credited | ON raw rounds |
|---|---|---|---|---|---|
| worldfire d5 s2002 | 60 | 118 (57) | 5.567 | **4.500** | 6.467 |
| worldfire d5 s3003 | 60 | 135 (60) | 5.483 | **4.300** | 6.550 |
| worldfire d3 s2002 | 15 | 34 (15) | 5.533 | **4.533** | 6.800 |
| worldfire2hg d5 s2002 | 30 | 39 (30) | 5.733 | **4.633** | 5.933 |
| worldfire d0 s2002 | 1000 (390 won) | 1389 (753) | 8.344 | 7.977 | -- |

Direct run, d5 b20 seed 9100001, 64 games: OFF 5.547, ON 4.422 credited.

So the rule credits Worldfire ~1.0-1.2 turns (2HG ~1.1), and every Worldfire game at d5 takes at least
one free turn. **The RAW round gets SLOWER than the flag-off game (+0.4 to +1.3)**: paired per game, raw
on - off is 0 in 38 / 60 games but reaches +5..+10 in a few (s2002 gi58: off T5, ON credited T5 with 10
free turns, raw 15; gi1: credited T3 after 8 free turns, raw 11). Nothing in the search prefers fewer
turns TAKEN at an equal credited round, and with no cap a lock that holds (the opponent draws only lands
after Worldfire) is time the deck spends rebuilding for free. Not a defect of the implementation; a
question for the USER (below).

### (b) Free turns received by every other deck (expected ~0)

| deck | games with a free turn | trait | mechanism |
|---|---|---|---|
| Unpredictable Cyclone | 161 / 1454 (free = 1 each) | 1 | legitimate: Ulamog / Emrakul strip the opponent's lands; the game ends on the free turn |
| Shadowborn Apostle | 18 (52 free turns) | 1 | legitimate: Sire of Insanity's end-step discard |
| Burn | 5 / 3015 | **0** | **hole** (below) |
| Extra Turns | 5 / 1375 | **0** | **hole** (below) |
| Modern RW Burn | 1 / 1375 | **0** | **hole** (below) |
| every other deck | 0 | -- | -- |

**The hole: the passive opponent's cleanup discards its OLDEST cards first** (`opponentdeck::
EndOfTurnCleanup` sheds `hand.front()` down to 7). All three trait-0 decks PUT CARDS INTO THE OPPONENT'S
HAND: Goblin Guide's reveal (Burn, Modern RW Burn) and Howling Mine / Horn of Greed (Extra Turns). The
hand passes 7, the cleanup sheds the opening hand's nonlands (the left end, the two the hurdle
guaranteed), and against the land-heavy cycling lists (Galadriel slot 16, Fluctuator slot 14) the hand
that is left can be all lands -> "no nonland in hand" with lands out and no creature -> locked. Repro:
`MTG_FREE_TURNS=1 build/Release/mtg decks/Burn/Burn.txt --profile decks/Burn/Burn.profile.json --seed 3664
--game-index 661 --games 1 --budget-ms 30` (credited T3, raw T4, vs Galadriel). A real player discards
excess LANDS, so the natural fix (agent proposal, NOT built): under `MTG_FREE_TURNS`, the passive cleanup
discards the leftmost LAND first and a nonland only when the hand holds no land. It moves every game where
our cards overfill the opponent's hand, so it needs its own measurement.

### (c) Whose play moved, and why

* Win turns moved (searched + d0 audit: 212 + 336 faster, 19 + 16 slower): almost all Worldfire (credit)
  and Cyclone / Shadowborn (credit). Per deck, games whose WIN TURN changed: Worldfire 394 faster;
  worldfire2hg 29 faster; Cyclone 75 faster / 6 slower; Shadowborn 17 / 2; Maze's End 13 / 4;
  Creature Burn 4 / 4; Midrange Burn 4 / 2; Burn 4 / 0, Extra Turns 3 / 0, Knights 3 / 0, Modern RW Burn
  1 / 0, Merfolk Turns 1 / 0; a handful of single-game moves (0-2 each) in ~20 other decks.
* **Hurdles**: every deck's per-game DIGEST moves in a few games (the opponent's opening hand differs in
  ~20% of games), but the win turn moves only in a few decks whose play depends on the opponent's
  lands or hand: Maze's End, Creature Burn, Midrange Burn, Knights, Merfolk Turns (mechanism per deck
  not traced). Those moves come with no free turn at all (Burn's / Extra Turns' / Modern RW Burn's are
  the trait-0 holes above).
* **Prune stand-down** (`deck_has_free_turn` disarms the turn-edge prunes): Vampires (Captivating
  Vampire's `steal_tap_count`, plus Worldfire in the list) and Sea Creatures (Kiora Bests the Sea God
  ch. 3) received ZERO free turns, yet play moved (Vampires 2 slower) and their search work rose (d).
  Captivating Vampire steals creatures only, which a passive opponent never has: it cannot lock, so the
  trait list over-reaches there (candidate fix: drop `steal_tap_count`).
* Reference replays under the flag: 1 play-drift of 940, `references/Creature_Burn/claude_s9_gi8.json`
  (replay no win vs ref T4). **Corrected in the second pass (below): NOT the flag.** It drifts identically
  with the flag OFF, on the shared branch since the opponent land-entry fix ce5866171; fixed there.
* Scenarios: 605 / 605 pass under the flag.

### (d) Search work, ON vs OFF (units, regression tier)

| deck | units ON / OFF | why |
|---|---|---|
| Worldfire / 2HG | 0.661 / 0.669 | credited wins sit inside a shallower window |
| Unpredictable Cyclone | 0.947 | same |
| Shadowborn Apostle | 1.143 | prune stand-down |
| Sea Creatures / 2HG | 1.257 / 1.202 | prune stand-down, no free turn ever |
| **Vampires / 2HG** | **3.06 / 2.63** | prune stand-down, no free turn ever (e.g. d3 s30001: same digest, 2.25M vs 0.91M units) |
| every other deck | 0.99 - 1.01 (all non-trait decks pooled: 1.001) | -- |

The per-deck stand-down is expensive where the trait over-reaches: Vampires pays 3x for a lock it cannot
make. Tightening `DeckMayLockOpponent` (or arming the stand-down only once a lock is reachable in the game)
is the obvious lever; not built.

### Open USER questions (from this measurement)

6. **Raw vs credited tie-break.** The search spends free turns without limit (raw +0.4..+1.3 vs OFF). Keep
   it, cap free turns per game (Q4, default none), or tie-break equal credited rounds on fewer turns taken?
7. **The cleanup hole** (b). **BUILT, flag-ON default, USER to confirm** (second pass (c)): not "lands first"
   but a **two-nonland floor**. The cleanup stays oldest-first and never sheds a nonland that would leave fewer
   than 2 nonlands; it sheds the oldest land instead. The unconditional lands-first version was built first and
   measured worse: it moved long games of nearly every deck.
8. **Trait over-reach.** **BUILT, flag-ON default, USER to confirm** (second pass (b)): `steal_tap_count` is
   dropped along with four other over-reaching params. `annihilator` is added. The sideboard counts only when a
   main-deck wish reaches it. Vampires / Sea Creatures units ON/OFF: 3.06 -> 1.000 and 1.257 -> 1.000.

## Second pass (2026-11-21/22, wip/freeturn; flags still DEFAULT OFF)

Three fixes from the first measurement, then one flag-ON regression tier (`reg_on4`). Commits on wip/freeturn
(rebased onto d0d5f8463): 9791b0c70 (stamps, trait audit, lands-first cleanup), a46be7cb2 (cleanup floor).

### (a) The reference drift was not the flag: recording stamps

`references/Creature_Burn/claude_s9_gi8.json` drifted with the flag OFF too. Bisected to the shared branch's
opponent land-entry rules (ce5866171): the game was recorded before them, its opponent's Gruul Turf now bounces a
land, and the recorded Price of Progress line no longer wins T4. Fixed by the repo's recording-stamp pattern:
* `opponent_land_entry: 1` on new recordings; an unstamped `opponent_land_model` reference replays with
  `--legacy-opponent-land-entry`, the pre-ce5866171 drop kept verbatim (also the old cross-head legend rule). That
  part landed on the shared branch separately as **d0d5f8463**.
* `free_turns: 1` on a recording made with `MTG_FREE_TURNS` on; an unstamped reference replays with
  `--legacy-no-free-turns` (`g_play_legacy_no_free_turns`, read in `FreeTurnsOn`), so the hurdles cannot change
  the opponent hand a reference was played against. Consequence: a flag-ON reference sweep replays every existing
  reference flag-off. On wip/freeturn only.
* Flag-OFF sweep with the fix: 940 refs, 0 play-drift. Flag-ON sweep (reg_on4): 940 refs, 0 play-drift.

### (b) Lock-trait audit (`GoldFishRunner::DeckMayLockOpponent`)

With the hurdles the opponent holds two lands and two nonlands, plays a land a turn, and never casts, so only a
card that can strip ALL its lands or ALL its nonland hand cards can earn a free turn. Audit per param:
* IN `exile_all_permanents_hands_graveyards` (Worldfire): every land and every hand card.
* IN `endstep_each_player_discards_hand` (Sire of Insanity): the whole hand at the opponent's end step.
* IN `annihilator` (Emrakul 6, Ulamog the Infinite Gyre 4), NEW: N permanents per attack, oldest first, lands
  included. This makes Extra Turns trait=1; its free turns are legitimate (s2629 gi627: Emrakul's annihilator 6
  strips all five opponent permanents, the next attack kills).
* OUT `combat_damage_each_discards` (Neheb): the engine never makes the passive opponent discard.
* OUT `steal_tap_count` (Captivating Vampire): steals creatures only.
* OUT `etb_destroy_nonbasic_land_optional` (White Orchid Phantom): the opponent fetches a basic in return.
* OUT the one-land removals: `etb_destroy_artifact_enchantment_land` (Acidic Slime),
  `sac_outlet_destroy_nonbasic_land` (Fulminator Mage), `saga_ch3_gain_control_opp_permanent` (Kiora ch. III),
  `cast_trigger_destroy_target_permanents` (Ulamog's cast trigger; Ulamog stays IN by annihilator).
* Sideboard: counted only when a main-deck wish can fetch from it (`DeckWishesFromSideboard`). Vampires holds
  Worldfire in a wishless sideboard.

### (c) The cleanup hole: the two-nonland floor (replaces lands-first)

Under `FreeTurnHurdlesOn` the passive cleanup (`opponentdeck::EndOfTurnCleanup`, `CleanupShedIndex`) stays
oldest-first, except it never sheds a NONLAND that would leave fewer than 2 nonlands in hand; it sheds the oldest
land instead. With the floor the opponent's nonlands fall below two only to one of OUR effects (the USER's "the
opponent needing to show that they can remove things"). An MDFC spell//land is a nonland in hand.

The first version (9791b0c70) shed lands first unconditionally. It closed the hole but moved long games of nearly
every deck, because it discards lands the opponent would have played. Readers of opponent lands (Price of Progress,
fetch life, Burgeoning) move. reg_on3 vs reg_on2 per-game digests: Burn 74 (6 slower), Modern RW Burn 35 (3
slower), Extra Turns 36, One Drop Burn 18, Maze's End 6 (5 slower), 1-3 in nearly every other deck.

The floor binds only when the hand is at 2 nonlands with a nonland oldest. Burn s3664 gi661 (the first-pass repro)
gives T4 under the flag, the hole stays closed. **reg_on4 vs reg_on2** (the run before any cleanup change), per-game
digest: identical in every deck except
* Burn 6: the 5 trait-0 hole games, now uncredited and equal to the flag-OFF GT (4 lose their credited turn,
  T3/T4 -> T4/T5; d5 s2002 gi714 keeps T4), plus d0 s2002 gi934 (T4 -> T5, no
  free turn). gi934 is the floor binding as specified: Goblin Guide's T1 reveal takes the opponent to 8 cards with
  exactly 2 nonlands, so the floor sheds the oldest land (an Arid Mesa) instead of a nonland. Off the rule the
  opponent cracks that Mesa for 1 life and T4 is lethal.
* Modern RW Burn 1: the hole game (d5 s3003 gi51, credited T3 -> T4, correct).
* Extra Turns 1 (no win-turn change); Vampires 25 / vampires2hg 6: the trait change, prunes re-armed.
* Maze's End, One Drop Burn, Worldfire, Cyclone, Shadowborn: 0.

So the lands-first long-game slowdowns are gone (Burn 6, Modern RW Burn 3, Maze's End 5 -> 0, apart from
gi934 above).

### (d) reg_on4 numbers (`MTG_PGO=0 MTG_FREE_TURNS=1 test/regression.sh --regression`, no accept)

Raw output in `build/scratch/freeturn/reg_on4/`, the per-key table in `logs/freeturn/analysis_reg_on4.txt`, the
run-to-run diffs in `logs/freeturn/diff_on2_on4.txt` and `diff_on3_on4.txt`. 174 pass / 215 fail vs the flag-OFF
GT (expected under the flag); scenarios 605/605; VPC 940 refs, 0 play-drift.

* **trait=0 free turns: NONE.** Every free turn is trait=1: Worldfire (887 games / 1678 free turns; 2HG 30 / 39),
  Cyclone 161, Shadowborn 18 (52), Extra Turns 5. Burn and Modern RW Burn: 0.
* Worldfire credited (vs OFF): d5 s2002 4.500 (5.567), d5 s3003 4.300 (5.483), d3 4.533 (5.533), 2HG 4.633
  (5.733); raw rounds 6.47 / 6.55 / 6.80 / 5.93 (Q6 unchanged).
* Units ON/OFF: Worldfire 0.661 / 2HG 0.669; Cyclone 0.947; Shadowborn 1.143 (legitimate stand-down); Extra
  Turns 0.992; **Vampires 1.000 / 2HG 1.014** (was 3.06 / 2.63); **Sea Creatures 1.000 / 2HG 1.000** (was 1.257 /
  1.202); every other deck 0.995 - 1.011.

### Agent defaults of the second pass (surfaced to the USER, not settled)

6. The cleanup floor: 2 nonlands, oldest-first otherwise (the agent's version of "discard lands before
   nonlands"). It binds at exactly 2 nonlands even where shedding to 1 could not lock (gi934).
7. The lock-trait list as audited in (b), including `annihilator` and the wish-reachable sideboard rule.
8. `--legacy-no-free-turns`: an unstamped reference replays flag-off, so a flag-ON sweep tests no reference
   under the rule until references are recorded with it.
