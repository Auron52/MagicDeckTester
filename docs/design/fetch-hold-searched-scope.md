# Searched fetchland hold: per-deck, landfall only (deferred audit)

**Status:** deferred (recorded 2026-11-16 during the Sea Creatures analysis). Not started.

## USER rulings (verbatim, 2026-11-16)

* *"We don't need the hold option for those decks or for this deck."* / *"(in searched)"* /
  *"The viewer can keep it."* (Creature Giving, Five Colour, Sea Creatures.)
* *"Hold should be per-deck and focused on cases with landfall."*
* *"Because the only reason to hold otherwise is clairvoyance."*
* *"For the time being we aren't worrying about the damage taken because it isn't usually relevant in
  goldfish but, if we were to do so, the best way to handle it would actually be in fetching shocklands
  tapped when we don't need the mana or fetching basics when we don't need the colours. Holding is not
  the correct solution to that problem."*

## What this means

* In SEARCHED (autonomous) play a battlefield fetchland is held only when the deck's provider opts in
  for a landfall reason. Every other deck cracks on play. A human in the play viewer may always hold
  (`HumanFetchChoiceOn`); that stays.
* Life loss is not a reason to hold. If life ever matters, the remedy is in the fetch TARGET and the shock
  choice: fetch a shock tapped (no 2 life) when its mana is not needed this turn, or fetch a basic when the
  shock's second colour is not needed.

## Owed audit

1. List every searched hold site: `DecisionProvider::ConsiderFetchHold` overrides per provider, and the
   root <=3-life autonomous hold (`FetchResolvesOnPlay` in `src/ai/DecisionProviders.cpp`, plus
   `ModernFetchCollapsible`'s life > 3 condition). The <=3-life hold is not landfall-scoped and is the
   "holding to save life" shape the USER rejected.
2. For each: is it landfall-motivated? If not, propose removal, plus for the life case the target/shock
   choice above, in place of the hold.
3. Measure the decks that move (held-out A/B, per-game movers explained) and bring the list to the USER
   before changing anything; this moves ground truth for every deck with a battlefield fetch.
