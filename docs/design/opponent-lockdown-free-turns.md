# Opponent-lockdown "free turns" (USER proposal, 2026-11-21)

Status: the USER's design (USER 2026-11-22 on adoption: *"Okay, but this is not a question."*), built behind `MTG_FREE_TURNS`, DEFAULT OFF until the parent flips it once the checks in "Third pass" pass; "Third pass" (l) states what the flip changes. The
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
   **ANSWERED (USER 2026-11-22): no ply cost** -- *"Let's try it this way for now. If we find this restrictive or
   troublesome we can re-adjust it."*

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
2. An MDFC spell//land in hand is a NONLAND card for the lock, a LAND for the hurdles. **SUPERSEDED (third pass):** an MDFC counts as
   the side the hand holds fewer of, a tie as a NONLAND (USER: *"Sure, that sounds fine."*).
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

6. **REPLACED by USER ruling 2a (third pass (c)).** The cleanup floor: 2 nonlands, oldest-first otherwise (the agent's version of "discard lands before
   nonlands"). It binds at exactly 2 nonlands even where shedding to 1 could not lock (gi934).
7. The lock-trait list as audited in (b), including `annihilator` and the wish-reachable sideboard rule.
8. `--legacy-no-free-turns`: an unstamped reference replays flag-off, so a flag-ON sweep tests no reference
   under the rule until references are recorded with it.

## Third pass (2026-11-22, wip/freeturn; `MTG_FREE_TURNS` still DEFAULT OFF)

Commits 94b9d46ca (gate on the lock trait, MDFC side rule, spawn land requirement, Sire lever + fairness check),
9d396adc8 (MDFC tie -> nonland, annihilator fairness, informational averages, `MTG_OPP_BOUNCE_OPENING`, per-job arms),
5267c39d0 (USER 2a cleanup by kind, USER 2b one-land removals in the trait). Units on 5267c39d0: 3896 / 3896 pass.
Measurements on 5267c39d0, `MTG_PGO=0`, raw output under `build/scratch/freeturn/` (`reg_on5/`, `fair/`, `scope/`,
`spawnrc/`, `bounce_*`), summaries in `logs/freeturn/`.

### (a) Everything is gated on the lock trait: no other deck moves

`freeturn::RuleArmed` = `MTG_FREE_TURNS` AND `GameState::deck_has_free_turn` (the deck's lock trait, stamped before
the deal). The credit, the hurdles, the cleanup rule and the spawn requirement (env unset) act only where it holds.
* **Flag-ON regression tier (reg_on5)**: 389 keys, 356 pass / 33 fail; **NON-TRAIT decks moved: 0**; every trait-0
  deck's search units ON/OFF are exactly 1.000. Scenarios 605 / 605; reference sweep 940 refs, 0 play-drift.
* Flag-ON smoke (on 9d396adc8): 227 / 16, non-trait moved 0. Flags-OFF smoke on 5267c39d0: 243 / 243, play-changed 0.
* Hence the Burn / Modern RW Burn games the earlier passes discussed (Burn d0 s2002 gi934, Modern RW Burn d5 s3003
  gi51, the five Burn "hole" games) are trait-0 and now byte-identical to the flag-off GT.

### (b) MDFC in hand: the fewer side, a tie as a NONLAND

`opponentdeck::CountHandSides`: cards by front face, then each spell//land MDFC joins the side the hand holds fewer of
(USER: *"a MDFC that is both should count as whatever you have less of"*); a tie joins the NONLAND side (USER
2026-11-22: *"Sure, that sounds fine."*): an opponent with a land out and a lone MDFC would cast its spell side, so a
tie read as a land could lock it spuriously. Used by the lock, the screw / flood hurdles and the cleanup.

### (c) The opponent's cleanup discards the more common kind (USER 2a)

USER: *"The opponent should discard whichever is more common between lands and nonlands including hand and field I
suppose. (not counting the creature tokens) That should avoid any weirdness as we will try to keep the thing we might
be missing."* `opponentdeck::CleanupShedIndex`, before every discard: count lands vs nonlands over the head's hand AND
battlefield (creature tokens excluded, an MDFC in hand by (b)), shed the oldest card of the more common kind; an exact
tie sheds a land (USER 2026-11-22: *"Defaulting to discarding land sounds fine."*). Replaces the two-nonland floor and
lands-first. Armed for trait decks only, so Burn gi934 (the floor case) is untouched.

### (d) One-land removals join the trait (USER 2b): Elementals, Prevent Damage, Sea Creatures flip to trait 1

USER: *"Yes, it should. ... Those decks need to lock out all lands and the turns where the opponent can play a land
would still not be free."* IN now: Acidic Slime, Fulminator Mage, Kiora Bests the Sea God ch. III, Ulamog's cast
trigger. Flipped 0 -> 1: **Landfall Elementals, Prevent Damage, Sea Creatures (+ their 2HG keys)**. reg_on5, none of
them takes a single free turn, but their play and cost move:
* Elementals: 16 games' digests, 1 win turn faster (d0 -0.001), units 1.002.
* Prevent Damage: 53 digests, 1 faster / 6 slower (d0 +0.002, d5 +0.007 / +0.013, 2HG +0.013), units 1.016 / 2HG 1.002.
  The spawn requirement accounts for its smoke moves (scope run (e): d0 gi9, gi259 slower only with it armed).
* Sea Creatures: 10 digests, 0 win turns, **units 1.257 / 2HG 1.202** -- the turn-edge prune stand-down
  (`NextTurnMayShareRound`), the same cost the first pass measured when Kiora was IN.

### (e) The spawn land requirement: global or free-turn-scoped (for the USER)

`MTG_OPP_SPAWN_NEEDS_LAND` (tri-state): unset follows the free-turn rule (armed for the lock-trait decks only, the
FREE-TURN-SCOPED option, which is what flipping `MTG_FREE_TURNS` ships); `=1` applies it to every game (the GLOBAL
option); `=0` is off everywhere. Root cause of the global slowdown, from replaying all 235 single-head smoke games whose
win turn moved under `=1` (both arms, `build/scratch/freeturn/spawnrc/`, `classify.txt`): **230 of 235 lose exactly the
turn-1 spawn**, and the other 5 are opponents with no land until T4 or later (gi537 in Burn / Critter / Shadowborn,
Anti-Lifegain gi407, Stuffy gi488) whose later spawns go too. The T1 spawn is lost in EVERY game, on the draw as well as
on the play (29 of the 235 are on the draw): the passive opponent plays its first land at the end of OUR turn 1 even when
it is on the play, so it never controls a land at our first untap. 200 of the 235 are slower, 35 faster. 162 of the 200
slower games cast one of six cards in the OFF arm that USE an opposing creature: Searing Blaze 69 (Burn 21, Midrange
Burn 35, Modern RW Burn 9, Creature Burn 4), Searing Blood 24 (Burn), Blasphemous Act 35 (Stuffy: one mana cheaper per
creature), Hinata, Dawn-Crowned 16 (a spell costs 1 less per target), Defense of the Heart 11 (Creature Giving: needs 3
opposing creatures), Swords to Plowshares 7 (Anti-Lifegain). The other 38 (36 at d0, plus Stuffy d3/d5 gi23) change line
with none of the six cast. Named games: Burn d5 gi233 (T4 -> T7: the opponent has no land until T4, all three
spawns vanish, the OFF arm's two Searing Bloods for 6 have no target), Burn d5 gi55 (T4 -> T5: Searing Blood for 3 has
no target), Midrange Burn d5 gi25 (T4 -> T5: the T4 Searing Blaze deals no 3 to the face), Stuffy d5 gi25 (T4 -> T5:
Blasphemous Act costs one more), Hinata d5 gi49 (T5 -> T6), Creature Giving d5 gi3 (T4 -> T5: Defense of the Heart a
turn later). So handoff 4's suspicion holds and is wider: every deck that spends an opposing creature as a resource
loses the T1 one. In the smoke tier the GLOBAL option moves 58 of 243 keys (digest), 29 of them in aggregate, summed
+0.635 turns (slower): Stuffy +0.208 (3 keys) + 2HG +0.080, Burn +0.109 + 2HG +0.040, Midrange Burn +0.097, Creature
Giving +0.071 + 2HG +0.020, Hinata +0.013, eight more decks +0.001 to +0.009, Mirrorwing -0.027, Extra Turns and Merfolk -0.002 (faster); per game, searched slower 44 / faster 2, d0 slower 162 / faster 33. Under the FREE-TURN-SCOPED option only the
lock-trait decks see it: measured as one pooled batch of the 28 trait-deck smoke jobs, flag ON, armed vs `=0` (`scope/`): 17 keys
move (digest), 7 in aggregate, summed **-0.066** (FASTER: without the T1 spawn a board can lock earlier), Shadowborn d0 /
d3 / d5 -0.009 / -0.040 / -0.020, Cyclone d0 -0.008 / d5 +0.010, Extra Turns d0 -0.001, Prevent Damage d0 +0.002; every
Worldfire key is digest-identical; units armed / off 0.997. The trait-0 decks' slowdown above does not happen at all. A refinement either option could take (not built): on the draw, count the
opponent's own turn-1 land drop as made before our first untap (a real opponent on the play has played its land), which
restores the T1 spawn in the 29 on-the-draw games.

### (f) Sire of Insanity: in the trait, and the fairness check

Sire stays IN (USER: *"That is why it is in the list, because some decks literally lose to the card."*; *"Sire of
Insanity I suppose is a bit messy because that is the closest to locking them when it shouldn't. In the face of that
card you would drop all permanents you can on the field, but the opponent has no ability to do so."*; *"(and it
protects your board)"*). `MTG_FREE_TURN_SIRE` (default ON, `=0` takes Sire out of the trait). The fairness check the
USER asked to try (*"Okay, that sounds worth trying."*), `MTG_FREE_TURN_SIRE_FAIR` (default OFF):
`freeturn::NoteSireBeforeDiscard`, right before Sire's end-step discard in both worlds, vetoes the next lock when the
discarded hand held a creature the opponent could have cast (MV <= its lands, each coloured pip <= its lands producing
that colour). Check only, no state change.

### (g) Annihilator fairness (USER: *"If we do that rule for Sire we should also apply the same rule to Annihilator if even just to check if it would make enough sacrifice targets to keep a permanent."*)

`MTG_FREE_TURN_ANNIHILATOR_FAIR` (default OFF): when an annihilator sacrifice takes EVERY opposing permanent, count the
nonland permanent cards in the opponent's hand it could cast (each separately against its current lands, a generous
reading: a real opponent could not have cast all of them); if real + castable permanents exceed the annihilator count
it would have kept one, and the next lock is vetoed. Check only.

### (h) Fairness batch (one pooled batch, arms innermost in 20-game blocks, held-out seeds 7710001..)

`fair/manifest.json`, 485 jobs; deltas are paired per block vs the `on` arm (se over blocks).

| deck (setting, games) | off | on (credited) | fairness arm | games with a free turn on / fair | units on/off |
|---|---|---|---|---|---|
| Shadowborn d5 b20, 1000 | 4.758 | **4.747** (-0.011, se 0.005) | sirefair 4.752 (+0.005 vs on, se 0.002) | 13 / 9 | 1.175 |
| Shadowborn 2HG d3 b10, 200 | 4.990 | 4.990 | sirefair 4.990 | 0 / 0 | 1.159 |
| Cyclone d5 b20, 1000 | 4.780 | **4.715** (-0.065, se 0.010) | annfair 4.750 (+0.035 vs on, se 0.008) | 286 / 114 | 0.942 |
| Cyclone 2HG d3 b10, 200 | 5.075 | 5.060 (-0.015) | annfair 5.075 | 8 / 2 | 1.029 |
| Extra Turns d5 b20, 500 | 4.734 | 4.736 (+0.002, se 0.004) | annfair 4.736 | 5 / 2 | 1.053 |

`sireout` (Sire out of the trait) is byte-identical to `off` on Shadowborn (units too): Sire is its only lock card. So
on Shadowborn the rule buys 0.011 turns and costs +17.5% search units (the prune stand-down); the fairness check keeps
9 of the 13 credited games (+0.005). On Cyclone the annihilator check cuts the credited games from 286 to 114 and gives
back more than half the credit (+0.035 of -0.065): most of Cyclone's annihilator locks are on opponents holding
castable permanents.

### (i) Informational averages, per-job arms, diagnostics

* `free_avg` / `extra_avg` (USER: *"include the average of the extra turns as a separate number, even if we don't
  optimize for it"*): on the batch per-job line (when nonzero), the `--games` summary and the tier report's `(info)`
  line. Never in the digest or GT. Games reused from a resumed batch's partial count 0.
* heurarm slots `MTG_FREE_TURNS`, `_SIRE`, `_SIRE_FAIR`, `_ANNIHILATOR_FAIR`, so one pooled batch carries every arm.
* `VPC_FREE_TURN_DIAG=1|2` (viewer_protocol_check.py): replay unstamped references under the rule (2: also under the
  spawn requirement). Diagnostic only, never in a tier.
* `MTG_OPP_BOUNCE_OPENING` (default OFF): the bounceland opening rule in every game (j).

### (j) The bounceland opening rule for every game (`MTG_OPP_BOUNCE_OPENING=1`, flags otherwise off; not accepted)

USER: *"we shouldn't be playing a bounceland first if we don't have an existing land unless forced by what the player
does. The basic replacing bounceland rule should apply."* An opening hand whose every land needs another land in play
(Karoos, Lake of the Dead) gets its rightmost such land replaced by a basic; the land-drop policy already plays a land
that can stay alone ahead of a Karoo (`OpponentLandDropIndex`), so this is the only case left. A drawn bounceland later
still returns itself. Measured: smoke 42 keys' digests move (1 game each, the gi whose dealt opponent is a Karoo list),
1 in aggregate (Merfolk d0 gi233 T6 -> T5, -0.001); regression 24 keys' digests, 2 in aggregate: Maze's End d5 s2002
gi168 T5 -> T6 (+0.005; MAZE_MECH) and Extra Turns d0 gi505 T6 -> T5 (-0.001). Scenarios 605 / 605, references 940, 0
play-drift (the references were all recorded on legacy boards, so none is dealt a Karoo opening). Adopting it would need
a recording stamp + `--legacy` flag like the other opponent-model changes, and a smoke/regression GT accept.

### (k) References under the rule: NONE changes

Diagnostic sweeps, flag ON, `VPC_FREE_TURN_DIAG=2` (rule + spawn requirement, the adoption picture) and `=1` (rule
only), `logs/freeturn/vpc_diag{2,1}.log`: both 940 refs, 364 ok / 575 repaired / 1 beaten / 0 play-drift, every per-ref
verdict identical to the flag-off sweep. Why: the rule acts only on lock-trait decks, and every reference of those decks
(Extra Turns 10, Prevent Damage 11, Sea Creatures 10, Shadowborn Apostle 10, Unpredictable Cyclone 10, Landfall
Elementals 10; Worldfire has none) was recorded on the legacy passive-opponent board (no `opponent_land_model`), where
no opponent library is dealt, so no hurdle, cleanup, spawn requirement or lock can act. **No reference needs
correcting; nothing to approve.** The legacy replay path stays (`--legacy-no-free-turns`,
`--legacy-opponent-spawn-any` for unstamped recordings).

### (l) EXACTLY what flipping `MTG_FREE_TURNS` to default ON changes

Only the lock-trait decks' games (Worldfire, Unpredictable Cyclone, Shadowborn Apostle, Extra Turns, Landfall
Elementals, Prevent Damage, Sea Creatures, and their 2HG keys). Every other deck is byte-identical (a). With the shipped sub-defaults (hurdles ON, spawn requirement follows the rule, Sire IN, Sire-fair and
annihilator-fair OFF, bounce opening OFF everywhere except as the hurdles' third step), the regression tier (reg_on5)
moves exactly these 33 keys, credited avg GT -> ON:

| deck | keys (GT -> ON) | units ON/OFF |
|---|---|---|
| Worldfire | d0 8.344 -> 7.977 (-0.367); d3 5.533 -> 4.533 (-1.000); d5 s2002 5.567 -> 4.500 (-1.067); d5 s3003 5.483 -> 4.300 (-1.183); backstop d3 / d5 s30401 6.000 -> 5.000 | 0.661 |
| Worldfire 2HG | d5 5.733 -> 4.633 (-1.100) | 0.669 |
| Cyclone | d0 -0.035; d3 -0.053 / -0.093; d5 -0.093 / -0.073 | 0.938 |
| Shadowborn | d0 -0.040; d3 -0.027 / -0.067; d5 -0.013 / -0.040 | 1.133 |
| Extra Turns | d0 -0.011; d5 -0.007 / +0.007; d3, 2HG digests only | 0.962 / 2HG 1.030 |
| Prevent Damage | d0 +0.002; d5 +0.007 / +0.013; 2HG +0.013; d3 digests only | 1.016 / 1.002 |
| Landfall Elementals | d0 -0.001; d5 x2 digests only | 1.002 |
| Sea Creatures | d0, d5, 2HG digests only | 1.257 / 1.202 |

Worldfire raw rounds (uncredited) under the rule: d5 6.47 / 6.55, d3 6.80, 2HG 5.93 (Q6, unchanged: no cap, no
tie-break). Free turns taken (reg_on5): Worldfire 887 games / 1678 free turns (2HG 30 / 39), Cyclone 161 / 161,
Shadowborn 27 / 71, Extra Turns 5 / 5, the three 2b decks 0. Smoke (on 9d396adc8, before 2a / 2b) moved the same decks'
16 keys the same way (Worldfire d5 5.725 -> 4.550). The flip therefore needs a GT accept of those keys in smoke,
regression and overnight (the overnight tier at its next periodic run), and new recordings get the `free_turns: 1` and
`opponent_spawn_land: 1` stamps; existing references replay unchanged (k).

### Agent defaults of the third pass (taken; listed so a reader can find them)

9. The spawn requirement counts the opponent's lands at OUR untap, so the T1 spawn never appears (on the draw too: the
   passive opponent's first land enters at the end of our turn 1). Refinement in (e), not built.
10. Annihilator-fair counts each castable nonland permanent separately against the current lands (generous) and fires
    only when the real sacrifice took every opposing permanent.
11. White Orchid Phantom stays OUT of the trait (the opponent fetches a basic for the land it loses).

### USER decisions open after the third pass

* **Spawn requirement scope** (e): global (`=1`; 58 smoke keys move, +0.635 summed, through Searing Blaze / Blood,
  Blasphemous Act, Hinata, Defense of the Heart, Swords losing the T1 creature) vs free-turn-scoped (what the flip ships;
  trait decks only, -0.066 summed).
* **The fairness levers** (h): Sire-fair keeps 9 of Shadowborn's 13 credited games (+0.005 turns); annihilator-fair cuts
  Cyclone's credited games 286 -> 114 (+0.035 of its -0.065). Both ship OFF unless the USER wants them.
* **The one-land-removal decks' cost** (d): with 2b, Sea Creatures pays +26% search units (2HG +20%) and Prevent Damage
  moves a few games slower, with zero free turns in any of their games. The stand-down is sound (a free turn would break
  the prunes' round + 1 assumption); the cheaper alternative would be to keep those cards out until a deck that can
  strip every land joins the suite.
