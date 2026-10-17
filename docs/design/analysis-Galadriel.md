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

## Scry policy — `GaladrielProvider::ScryKeepOnTop` (USER rules, 2026-10-17; `MTG_GALADRIEL_SCRY`, default ON)
A DEFAULT, not a narrowing on its own: the land drop's scry stays searched. Inside a reveal chain the
rule is candidate 0 (walked live, per look); `MTG_GALADRIEL_CHAIN_PRUNE` (default ON pending A/B) prunes
the chain to it alone outside human play.
- **Galadriel out** (USER): "You keep any scry land on top and pitch other things except for Crackle when
  you have enough mana out to finish the opponent next turn." (`GaladrielCrackleLethalNextTurn`.)
- **Galadriel not out** (USER): "Without Galadriel, you want to look for colors, Crackle and acceleration.
  Anything else is thrown unless you are short on lands and when you are really in bad shape for colours
  you might even throw Crackle or acceleration if they don't help solve the problem." Encoded as: colours =
  G, U (Galadriel) and R (Crackle), covered by any land/rock on the battlefield or in hand (a filter covers
  all); a top card covering a missing colour is kept; "bad shape" = G or U missing, and then Crackle and
  acceleration (Sol Ring, Skerry) are thrown unless they cover a missing colour; "short on lands" = under
  three lands on the battlefield + in hand, and then any land is kept; everything else is thrown.
  AGENT READING (surfaced): a Galadriel on top while none is in hand or on the battlefield is kept.
- BUG FIXED with this change: `x_damage_multiplier` DEFAULTS TO 1, so the first version's "Crackle"
  test (`> 0`) matched every card (every hand card counted as a held Crackle; with Galadriel out a Sol
  Ring / Skerry on top went down the Crackle-lethal path). Now `IsCrackle` = multiplier > 1.

## Turn order — `GaladrielProvider::LandEntrySpotsRule` (USER 2026-10-17; `MTG_GALADRIEL_SPOTS`, default ON)
USER: "the typical order should have Galadriel before lands and Crackle after. It might sound weird, but
this works the best." Galadriel is the deck's only land-entry watcher (`PutsLandfallWatcher` includes
`scry_reveal_land_to_battlefield`), so the Landfall deck's SPOTS rule expresses it: the land right after
her cast (land_slot k=1, payable prefix) takes the preferred list position, the land-first plan stays
searched, and Crackle (dispatched after the land) is sized with the chain's lands in play. Human play keeps
every position. NOT YET COVERED: the beyond-horizon playout / depth-0 runner still play the land first.

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

## Stage 4 — baseline profile (2026-10-17, on 48bf0607 + the GALADRIEL_SCRY heurarm slot)
`decks/Galadriel/Galadriel.profile.json`. Card scores: Galadriel 1st copy +2.07 (2nd -0.07, the legend
rule), Skerry +0.72, Sol Ring +0.63, Crackle 1st +0.23 / 2nd -0.82. Cost diagnostic COST_NEUTRAL.
Discard: DISCARD_UNLABELLED -- 0 real sheds, 17,823 rollout sheds / 400 games (13,183 under 4 lands);
owed: the MTG_SHED_WORST bound.
**USER decision (2026-10-17): Galadriel is a REQUIRED PIECE** ("We should probably make Galadriel a
required pieces"; after the user noted "it's not difficult to lose the game if you keep a no-Galadriel
hand"). Applied with min_playable 0 -> 1 (the skill's multi-colour floor); stop_at stays 4.
Measured, seed 63000: d0 4000 games 7.8625 -> 7.8450; d3 b20 300 games 5.5733 -> 5.5567.

## Stage 5j — cost gate: OVER (2026-10-17, first reading)
Budget = 3x pd = 8,085.6 ms/game. Galadriel measured ~9,900 ms/game at d3 b20 (300 games, 30 threads) --
OVER before the d5 case is even measured. Per CLAUDE.md the deck is NOT added to the suite; getting it
into range is now the FIRST goal (ahead of value leaf / mulligan). Post-payment-fix profile is flat
(BuildSimKey ~7%, CollectActions ~4%, the chain's ScryTop/FireScryWatchers/ScryKeepOnTop ~6%).
Next: measure the chain axis' share (MTG_SCRY_SEARCH=0 diagnostic) and the k-variant census.

## RESUME POINT (2026-10-17, before a context compaction)
Committed: 48bf0607 (cards, Galadriel engine, Grey Havens, MTG_ACF_EXACT / MTG_ACF_INTERIOR payment
fixes, viewer wiring, tests, ledger). NOT pushed -- asked the user whether to push + watch CI (Windows);
no answer yet, so hold.
UNCOMMITTED, in flight:
- `decks/Galadriel/Galadriel.profile.json` (untracked): baseline profile + USER's required piece
  (Galadriel) + min_playable 1.
- heurarm slots GALADRIEL_SCRY and GALADRIEL_CHAIN_PRUNE (src/ai/HeuristicArm.h).
- **USER's chain rule (2026-10-17)**: "We definitely want a scry heuristic. With Galadriel out it is
  simple. You keep any scry land on top and pitch other things except for Crackle when you have enough
  mana out to finish the opponent next turn." Implemented in GaladrielProvider::ScryKeepOnTop (gal_bf>0
  branch; GaladrielCrackleLethalNextTurn: all sources untap + a hand land, X = ceil((life-power)/5), cost
  3X+2 + one per {R} short of native red sources, needs filters to cover the shortfall). Pre-Galadriel:
  the agent's draft rule (unmeasured).
- Chain mechanics changed: ScryChainCandidates = {-1 = LIVE provider default (ScryKeepOnTop per look on
  the current board), then k = 0..S}; GaladrielProvider::ScryChainSingleCandidate prunes to the default
  alone (MTG_GALADRIEL_CHAIN_PRUNE, default ON pending A/B; never in human play). Test updated
  (pinned skip now index 2).
- A rebuild + full unit-test run was in flight (background) when compaction was requested.
Measured so far (seed 63000 unless noted): d3 b20 ~9.9 s/game; d5 b40 ~12.4 s/game (seeds 61000/62000,
draft rule, unpruned) -> Stage 5j cost gate OVER (budget 8,086 ms/game = 3x pd). Deck NOT in the suite.
NEXT, in order:
1. Confirm build + unit tests green; smoke must stay byte-identical (only Galadriel code changed).
2. One pooled batch, fresh seeds (61000/62000), d0/d3/d5: arms = {generic scry (GALADRIEL_SCRY off),
   USER rule unpruned (CHAIN_PRUNE off), USER rule pruned (default)} -- report avg AND ms/game to the
   user; the prune is a perf lever whose quality must hold.
3. Cost gate again (suite_gate --cost semantics: worst searched case, regression-row settings
   d3 b20 / d5 b20). If still over: next suspects are per-node costs on 30-40-permanent boards
   (BuildSimKey ~7%, CollectActions ~4%) and remaining branching (land-choice x cast subsets).
4. Then Stage 5 battery (5a nonconv/fd-diverge, 5b depth sweep, 5c2 tie-break, 5d claude-play
   sweep, 5h viewer audit, discard MTG_SHED_WORST bound), Stage 4-bis/5j suite add once under 3x.
Open user questions: push 48bf0607 + watch CI? Field audit found 2 OTHER-deck keyword mismatches
(Saproling Migration: Kicker; Kili, the Resourceful: Storied) -- untouched, reported. 3 Temples still
need `audit_card_fields.py --update` (429s); cost audit should be re-run clean later.
Gotchas: the 9p clock lags ~195 s and most files are root-owned (touch fails) -> wait ~200 s after an
edit before ./build.sh. Never `pkill -f <pattern>` -- it matches its own shell; kill by pid.
