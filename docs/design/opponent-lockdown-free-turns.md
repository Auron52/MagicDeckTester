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

* A real opponent library + 7-card opening hand (`core/OpponentDeck.h`, `opponentdeck::Deal`),
  drawn from the 50-deck pool when the deck observes land development, with a deterministic
  land-play policy (`GameState::opponent_plays_lands`) and an end-of-turn opponent draw. The rule
  needs this deal ON for any deck it applies to.
* Scheduled creature spawns (`GoldFishRunner::PopulateOpponentSpawns`): all arrive on turns 1-3.
  They are opponent creatures for condition 2.
* Extra-turn accounting (`extra_turns_taken`, `TurnSeq()` = turns taken, `turn_number` = round).

## Open questions (USER), with the provisional default taken

1. **Scope.** Applied universally, a land-free opening 7 (the opponent never mulligans; a few % of
   games) in a no-spawn game (patterns 0-1 = 20% of games) would give EVERY deck free turns for the
   opponent's natural mana screw -- not a lockdown. DEFAULT: the rule is armed only for decks holding
   a lock card (a deck trait stamped from card params; Worldfire today), so every other deck stays
   byte-identical. Alternative: universal + an opponent mulligan rule (moves every deck's GT).
2. **Reading of condition 1.** DEFAULT: "(no lands on battlefield) OR (no nonland in hand)", AND
   condition 2.
3. **2HG.** DEFAULT: every opposing head must be locked (each evaluated on its own zones).
4. **Cap.** DEFAULT: none; a free turn only matters in a game that is won.
5. **Search depth.** Depth counts rounds, so a free turn would cost no ply and a long lock would
   let the search go many turns further inside the same depth (cost blow-up). DEFAULT: a free turn
   costs a ply like a normal turn (search shape unchanged); only the reported/compared win turn
   gets the credit.

## Measurement plan

Worldfire is reported under BOTH metrics (raw rounds and free-turn credited) so the USER can see
what the rule buys. The rule ships default-on for armed decks only after the USER answers (1).
