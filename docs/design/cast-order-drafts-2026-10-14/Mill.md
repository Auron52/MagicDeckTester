# Mill: cast-order draft (2026-10-14)

**This deck is not ready for play.** None of its 11 nonland names is in cards.json, and only 5 of its 21 lands are (3 Island, 2 Scalding Tarn). Swamp, Watery Grave, Darkslick Shores, Drowned Catacomb, Polluted Delta and Oboro are missing. In probe s1001 gi0 the deck made two land drops in eight turns and cast nothing, so the game went unwon (avg 9). No game can be won, because nothing implemented mills or deals damage. The order below is **provisional, written from Scryfall oracle text**, to steer the implementation.

## 1. Status: **(D)**, generic tiers only
* Provider: **AntiLifegain**. cast-order-rankings.md:693-695 reads *"No implemented non-land cards -- nothing to review yet"*. There is no flag, no ledger, and no USER ruling.
* **Not in `test/regression_cases.sh`**: no case and no ground truth.
* **Routing is ACCIDENTAL.** Scalding Tarn's `fetch_land_types` sets the `anti` signature on its own (DecisionProviders.cpp:18522). It is the only signature-bearing param this list carries. The pin `{"Mill", "AntiLifegain"}` (test_pirates_provider.cpp:121) was *"pinned from a direct --batch probe ... so a future signature cannot capture them unnoticed"*: a measurement, not a decision. This is the fetchland misroute class FiveColour hit (:18476). analysis-Pirates.md:470-490 moved Pirates off this same AL capture as "a misroute".
* **What it inherits from AL today:**
  * The order itself does not change. AL's CastOrderRank overrides (Swords 21, alt-lifegain creature 24) and its MainPhaseOverride match no card in this list.
  * The single-pick, colour-coverage fetch (`FetchCandidates`, :4537) is **live**. It is tuned to AL's 4-colour shell ("black once, white twice"), and it will choose Polluted Delta's and Scalding Tarn's targets.
  * The USER's AL cleanup-discard buckets (:5411) are **live**.
  * `ClassifiesMainPhases()=true` is dormant only while `uses_second_main=no`.

## 2. What plays today
| pos | card(s) | main | rule |
|---|---|---|---|
| 0 | land drop | m1 | First. A searched plan also plays its land before the casts (TurnSolver.cpp:39661; only a Karoo is deferred). |
| - | every nonland card | - | not in cards.json |

Implemented as-is, generic would rank the Crabs at 10, after a land drop that is always first, and every spell at 20 (**TIE**, settled by hand order). Visions of Beyond (`draw`) is ORDER-OPAQUE: the rank sort is skipped for any set that contains it.

## 3. Proposed order (single main: every cast m1; the 0-power Crabs never matter to combat)
| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 1= | Hedron Crab, Ruin Crab | m1 | **[before land .. after land]** | "Landfall -- whenever a land you control enters, [target player / each opponent] mills three." Cast them BEFORE the land drop, so this turn's land (and its fetched land) triggers them. Walk to after the land only when the Crab needs that land's mana (turn 1, or every land tapped). **The tie is safe:** both triggers are independent and identical in size. |
| 2 | land drop | m1 | - | After the Crabs. A fetchland is cracked at once, so both lands entering trigger every Crab on the battlefield. |
| 3= | Glimpse the Unthinkable, Fractured Sanity, Archive Trap, Tasha's Hideous Laughter | m1 | - | The bulk mill: 10, 14 and 13 cards, and Tasha's exiles until MV 20. Archive Trap is always a hard cast ({3}{U}{U}), because the passive opponent never searches. **The tie is safe:** every count is fixed and none reads another's result. Tasha's exiles rather than mills, so it feeds no graveyard reader, and pip conflicts are the payer's job. |
| 4 | Surgical Extraction | m1 | - | It needs "target card in a graveyard other than a basic land", so it goes after the turn's mill. Exiling every other copy of a milled name from the library is three more cards gone. Cost {B/P}, payable with 2 life. |
| 5 | Visions of Beyond | m1 | - | "Draw a card. If a graveyard has twenty or more cards in it, draw three instead." It goes after the turn's mill, so the mill can push a graveyard to 20. **State-conditional:** once a graveyard is already at 20 or more, it moves to the draw-first slot (principle 1), ahead of the Crabs. |
| - | Counterspell, Drown in the Loch, Fatal Push | - | - | Never cast against the passive opponent: no spell to counter, and the only creatures are inert spawn bodies. Their rank cannot matter. |

## 4. What changes and why it matters (predicted; none of these cards exists yet)
1. **Crabs before the land drop.** T3: two Islands untapped, hand 2x Hedron Crab + Polluted Delta. Crab, Crab, then Delta: 12 milled with both landfalls, 6 the way the engine models the fetch. Land first (today's only shape) mills 0. The same loss recurs on every turn a Crab is cast, which is the deck's whole T2-T4 clock.
2. **Visions after the mill.** T5: opponent graveyard at 12, hand Glimpse + Visions. Glimpse puts the graveyard at 22, so Visions draws three. If Visions goes first, it draws one, and that is exactly what the generic ideal-order cantrip tier (rank 2, mv <= 1) would do if adopted.
3. **Surgical after the mill.** In a hand-order tie Surgical can come first, with no milled target yet (early on, the opponent's graveyard is empty). After the mill it strips up to three more cards from their library.

## 5. Questions for the USER
1. Should the Crabs go before the land drop, walking to after it only when they need its mana? Default: **yes**.
2. Should Visions of Beyond go after the turn's mill, but first once a graveyard is already at 20? Default: **yes**.
3. Should Surgical Extraction go after the mill, targeting the milled name with the most copies left in their library? Default: **yes**.
4. Should the deck get its own `MillProvider : DeckProvider`, routed above `anti` by mill-only params (never by fetchlands), as with Pirates? Default: **yes**.

## 6. Generic-order findings
* **G1. The land drop can never follow a cast in a searched plan** (TurnSolver.cpp:39661; only a Karoo is deferred). That makes "watcher before the land" inexpressible at every budget. It is the third member of the open land-two-position family, after Mirrorwing's start-of-turn / after-draw rule and Creature Giving's Sylvan Scrying (whose `LandDropAfterHandLandTutor`, DecisionProvider.h:1462, is depth-0 only).
* **G2. A threshold draw breaks "draws first".** Visions wants to go after the cards that fill a graveyard. The ideal-order cantrip promotion needs an exception keyed on a "draw more if a graveyard has >= N" param.
* **G3. A spell that needs a graveyard target ties with the spells that create its targets** (both rank 20).

## 7. Implementation notes
* Params:
  * Crabs: a new landfall-mill trigger (target / each opponent, 3).
  * Mill spells: a new opponent-mill count (Fractured Sanity also needs cycling-mill 4).
  * Tasha's: exile until MV >= 20.
  * Surgical: same-name exile from library.
  * Visions: `draw: 1` plus a threshold param.
  * Counterspell / Drown / Push: `goldfish_inert`.
  * Every mill param must also be read by `GoldFishRunner::DeckTouchesOpponentZones`, or the opponent's library is never dealt (OpponentDeck.h `Deal(needed)`). That library is a fixed 60-card list (53 after the hand), and the mill win is `opponent_decked` (CR 104.3c).
* Ranks: Crabs 1; mill spells 20; Surgical 22; Visions 23, or 2 when a graveyard is at 20 or more (CastOrderRank receives the state). Visions is ORDER-OPAQUE (`draw > 0`), so the provider must return `OrderOpaqueCastsByRank() = true` for its rank to bind.
* **ENGINE LIMITATION:** slot 1 needs the land position to become a plan-level choice, searched like `fetch_target` / `land_face` ([after watchers .. first]). A cheaper route is opening a main 2 and holding the land for it, but that costs a second solve per turn.
* **MODEL GAP:** `PerformFetch` resolves a fetchland on play, so the fetchland itself never enters the battlefield. Every fetch therefore loses one landfall trigger (3 cards per Crab). Oboro's "{1}: return to hand" is the deck's replayable landfall and needs an activation.
