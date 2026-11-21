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
