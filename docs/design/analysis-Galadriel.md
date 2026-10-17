# Analysis ledger — Galadriel

Deck: `decks/Galadriel/Galadriel.cod` — 4 Galadriel of Lothlórien, 2 Crackle with Power, 1 Sol Ring,
and 53 lands: 35 Temples (scry lands, enter tapped), 4 Crystal Grotto, 4 Rumble Arena, 4 Zhalfirin
Void, 2 The Grey Havens (untapped scry lands) and 4 Saprazzan Skerry (no scry). The engine of the deck
is Galadriel's "whenever you scry, reveal the top card; a land goes onto the battlefield tapped": with
her out, one land drop chains through the library. The finisher is Crackle with Power (X = 4 for 20 is
14 mana), plus Galadriel's 3/3 attacks.

Analysis started 2026-10-17 in the primary checkout, branch `phase-1-2-deck-analyzer`.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks. Model the full card. Goldfish-inert deferrals
  stay PROVISIONAL until the user signs off.
- No greedy pick inside the search window (user hard rule). The Galadriel chain is SEARCHED as one
  skip-count policy per chain (see Stage 2). Its two dominance collapses are PROVISIONAL.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time.

## Stage 1 — coverage (2026-10-17)
Missing (13): Galadriel of Lothlórien, Crystal Grotto, Temple of Abandon / Deceit / Enlightenment /
Malady / Malice / Mystery / Plenty / Triumph, The Grey Havens, Zhalfirin Void, Rumble Arena.
Present (4): Crackle with Power (Hinata), Temple of Epiphany (Treasure Hunt), Sol Ring, Saprazzan
Skerry. No sideboard. Routed to `GenericProvider` before this analysis.

## Stage 2 — cards

| Card | Tier | Model | Deferrals (status) |
|---|---|---|---|
| 8 new Temples | 1 | `basic_land`, two colours, `enters_tapped`, `etb_scry 1` (the Temple of Epiphany shape) | none |
| Zhalfirin Void | 1 | untapped {C}, `etb_scry 1` | none |
| Crystal Grotto, Rumble Arena | 1 | `any_color_filter` (the Capital City shape) + `etb_scry 1`; Rumble Arena's Vigilance is on a land that is never a creature (inert, carried) | none |
| The Grey Havens | 2 | {C}, `etb_scry 1`, Legendary; NEW `mana_colors_of_graveyard_legends`: `EffectiveProduces` adds every colour of a legendary creature card in our graveyard (`GraveyardLegendColors`), and the four payment paths that inline their own colour logic were taught it (the DFS payer, its pruning bound, the payment memo hash, the source colour cap) | none |
| Galadriel of Lothlórien | 3 | NEW `scry_reveal_land_to_battlefield`: `FireScryWatchers` at the tail of `ScryTop` (the one scry chokepoint, both worlds); a revealed land enters tapped through `EnterLand` and its own ETB scry re-enters `ScryTop` (the chain) | Ring-tempt scry 3: deferral APPROVED by the user 2026-10-17 (no card in the pool tempts) |

### The Galadriel chain: how it is searched
- Every look in a chain is a choice, so a chain is ONE searched decision. Two PROVISIONAL dominance
  collapses for the autonomous search: (a) a scry-land on top is always kept (bottoming it reveals the
  same next card with one fewer land and no look at it); (b) "you may reveal" is always taken.
- What remains is a skip count k: bottom the first k STOP cards (nonlands, and Saprazzan Skerry, which
  ends the chain when taken) and stop at the next. `ScryChainCandidates` lists k = 0..S (S = stop
  cards in the library), the provider default first (`ScryKeepOnTop` walked as a k).
- The pin is the land drop's existing `Plan::scry_choice` (consumed by the first look, which is the
  dropped land's own scry), so the chain rides every site that already installs it. The axis is not
  width-capped for a chain.
- Galadriel counts as a land-position watcher (`PutsLandfallWatcher`, `LandOrderMatters`), so "cast
  Galadriel, then play the land" is enumerated: a land played after her starts a chain.
- Viewer: every look is the normal `scry` decision; a revealed land asks `dig` with source
  `(galadriel reveal)` (default reveal, -1 declines).

## Open questions for the user (work continues on the stated default)
1. ~~Ring-tempt deferral~~ — APPROVED by the user 2026-10-17 ("That's fine on the Ring-tempt clause").
2. PROVISIONAL dominance collapses in the chain (keep scry-lands; always reveal). [default: collapsed
   for the search; the human keeps both choices]

## User direction (2026-10-17)
- "Realistically we will want some smart scry heuristics." → owed: a `GaladrielProvider` scry policy
  (the per-look `ScryKeepOnTop` outside a chain AND the chain's default skip count, candidate 0 of
  `ScryChainCandidates`). It ORDERS only (the search still branches every k), so it is a provider
  default, but it is authored, measured (with/without A/B), and presented to the user before adoption.

## Scry policy draft — `GaladrielProvider::ScryKeepOnTop` (2026-10-17, `MTG_GALADRIEL_SCRY`, default ON pending measurement)
A DEFAULT, not a narrowing: the land drop's scry stays searched, and a chain's candidate 0 is this rule
walked as a skip count, with every other k still enumerated.
- Galadriel: keep only while none is on the battlefield or in hand (a second copy is legend-ruled).
- Crackle: keep while none is in hand; a second copy is bottomed (X=4 off one cast is 20 for 14 mana;
  two X=2 casts are 20 for 16).
- Sol Ring: keep only while short of Galadriel's three mana.
- Land, no Galadriel out: keep while lands on battlefield + in hand (+2 per rock) are under three.
- Land with Galadriel out (only Saprazzan Skerry reaches the rule mid-chain): bottom it, so the chain
  continues.
To measure: generic vs this rule, paired seeds, d0/d3/d5; report to the user before calling it adopted.

## Unit tests (2026-10-17)
`test/unit/test_galadriel.cpp` (6 cases: printed data, chain to the first nonland, pinned skip, Skerry
ends / is skipped, no-watcher and gate-clear, Grey Havens colours). Two existing tests updated for the
new gate: `test_landfall.cpp` "no landfall card" also clears `deck_has_scry_watcher`;
`test_pirates_provider.cpp` pins Galadriel -> Galadriel.

## Performance — mana payment (2026-10-17; USER: "We should optimize the mana logic. This deck is not complex in that way.")
Symptom: the first profile run (card scores, 1000 games d5) stuck in a tail at ~2 of 32 cores; perf put
~96% of the time in `TapForCostBacktrackWorker`. Repro: Galadriel d5 b40 seed 2008 gi 6 (the
`--games 48 --seed 2002` gi=6 game).

Diagnosis (new diagnostic `MTG_TAP_STATS=1 MTG_TAP_SLOW_NODES=N`: dumps any top-level payment over N nodes):
every slow payment was Crackle with Power at X=4/5 on a 15-land + Sol Ring board.
1. UNPAYABLE {15}{R}{R}: 8-14 M nodes each. Total mana was exactly 17, but the only native red source
   was one Temple of Triumph; the second {R} needs a filter (Crystal Grotto / Rumble Arena) whose {1}
   feed leaves 16. The flow oracle priced any_color_filter as a FREE rainbow land, so it never proved it.
2. PAYABLE {12}{R}{R} / {15}{R}{R}: ~1.8 M nodes each to FIND the payment -- the oracle runs at the top
   only, so the DFS proved every dead partial tap set by exhausting it.

Fixes (both lossless + byte-identical -- they only cut subtrees that fail; smoke 120/120, play-changed 0):
- `MTG_ACF_EXACT` (default ON): when the gross oracle says feasible and an any_color_filter is present,
  an exact check tries k = 0..F filters in coloured mode -- their colour capped at k through a CONV node,
  generic demand + k for the feeds -- and proves infeasible if no k saturates. Sound: a real payment with
  j conversions is a flow of the k=j graph. The payable path's verdict and hint are untouched.
- `MTG_ACF_INTERIOR` (default ON): the same oracle at interior DFS nodes (reads current tapped flags +
  floating), armed only on a filter board, after 256 nodes of the payment, and never while the
  `MTG_FLOW_ORDER` hint is live.

Measured, gi 6: did not finish in minutes -> 21.1 s (ACF_EXACT) -> 9.6 s (+INTERIOR), same result (T6);
slow-payment nodes 157 M -> 0.87 M. Smoke makespan 79 s -> 70 s (another filter deck got faster).
Not yet done: identical-capability source folding (Grotto == Arena; the dup collapse keys on the
CardDefinition) -- only if a residual payment tail shows up again.
