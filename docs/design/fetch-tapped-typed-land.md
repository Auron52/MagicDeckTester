# Fetching a tapped typed land (Hedge Maze, Triomes): only when the mana is not needed this turn

Status: Sea Creatures flag built (`MTG_SEA_FETCH_MAZE`, default OFF), A/B measured below. The other decks are
**not** changed. All of this is for the USER to decide.

## The USER's rule (2026-11-17, verbatim)

- "Hedge maze is a maybe if we absolutely do not need it for mana. We can discuss more tomorrow."
- "(if we do not need it for mana this turn it is possible, otherwise not)"
- "Honestly, Triomes have a similar disposition where they appear."

Earlier statements of the same rule for Five Colour: "we probably should have a way to get triomes when we can
determine there is no need for the land to enter untapped" (2026-08-18), and "if there is a spell that requires
the land we should always go for untapped ... triomes are STRICTLY for times when you cannot use the mana"
(2026-08-19). For Unpredictable Cyclone: "Arguably the triome should be fetched if we don't need the mana"
(2026-10-27).

## Where each deck stands

| Deck | Tapped typed land | Today |
|---|---|---|
| Five Colour | Zagoth Triome, Jetmir's Garden | Already applies the rule. `FiveColourProvider::FetchCandidates` RULE 6: a land's "untapped" preference counts only while its mana is *spendable* this turn. A fetch whose mana is spendable ranks an untapped land first; otherwise breadth decides and a Triome wins on its third colour. The search sees only the top entry. |
| Unpredictable Cyclone | Xander's Lounge | Does **not** apply the "needed" test. `MTG_CYCLONE_FETCH_RULE` always searches the Lounge beside the rule's untapped pick, so the search decides every time. Applying the rule would *narrow* that: drop the Lounge when the mana is needed. |
| Sea Creatures | Hedge Maze | Was last in the one-option ladder (Pool > Island > Forest > Maze), so it was fetched only when nothing else was left. `MTG_SEA_FETCH_MAZE=1` applies the rule (below). |

## "Needed this turn": the test

This is Five Colour's RULE 6 test, reused in `SeaFetchManaNeeded`. It is a YES when either:

- **(a) Solo:** a hand card can't be cast from the current untapped pool, but could be with one more mana of a
  colour the untapped pick makes.
- **(b) Sequence:** that one more mana casts one more hand card in sequence (cheapest first).

Either YES means the mana is needed, and only the untapped land is offered. Otherwise the tapped land is
**offered beside** the untapped pick as a second searched option. It never replaces it, so the search still picks
between the two (searched-with-heuristics; never a forced pick).

What the test does not see:
- A land Aura's bonus mana on the fetched land itself.
- Activations that cost mana.

Both would only make "needed" true more often, so the error is on the side of fetching the untapped land.

## What a tapped typed land buys when the mana is idle

- **Surveil 1** (Hedge Maze) or a third colour (Triomes).
- **A library with one fewer tapped land.** A Maze or Triome drawn later costs a tempo; fetching it on a turn when
  the mana was idle removes that cost for free.
- It is still a Forest/Island (Sea), so it hosts a land Aura later.

## Sea Creatures implementation (`MTG_SEA_FETCH_MAZE`, HeuristicArm slot `SEA_FETCH_MAZE`)

`SeaCreaturesProvider` changes:
- `LandSearchFetchPickOrder` moves an admitted Maze to second place.
- `LandSearchFetchPickWidth` returns 2 when Maze is admitted.
- The legacy route `FetchSearchCandidates` returns {top, Maze}.
- `OnPlayFetchWidth` caps at 2.
- `LandSearchPickOrderKey` bits 8–11 carry the flag and the "needed" verdict for each possible top pick.

The flag only matters under `MTG_SEA_FETCH_PRUNE` (default ON). It is tested in
`test/unit/test_sea_creatures_mana.cpp` ("Sea fetch Maze").

## Measurement (2026-11-17)

Held-out paired A/B, Sea Creatures at d5 b20, `MTG_SEA_FETCH_MAZE=1` vs off, one pooled batch:

| Set | Games | On | Off | Delta (turns) | se | Faster | Slower |
|---|---|---|---|---|---|---|---|
| Standard (seed base 9400001) | 1000 | 5.8220 | 5.8270 | -0.0050 | 0.0022 | 5 | 0 |
| 2HG, 30 life, 2 heads (base 9500001) | 300 | 6.3567 | 6.3600 | -0.0033 | 0.0033 | 1 | 0 |

Six games change, each by exactly one turn faster (std gi 223, 383, 814, 935, 989; 2HG gi 183). No game is
slower. Search units per game are within 1% (std 1.1076 on vs 1.1186 off). The flag is rarely live, because it
needs a fetch, an untapped pick whose mana is idle that turn, and a Hedge Maze still in the library. See also
the Sea Creatures ledger (`docs/design/analysis-Sea Creatures.md`, "Hedge Maze fetch").

## Open questions for the USER

1. **Sea:** adopt `MTG_SEA_FETCH_MAZE` as default ON?
2. **Cyclone:** narrow the always-searched Xander's Lounge to "only when not needed", to match?
3. **Five Colour:** already does this. Its Triome is the only target when it wins (top-1). Should it instead be
   offered *beside* the untapped pick, as for Sea?
